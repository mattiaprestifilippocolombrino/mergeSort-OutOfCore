#pragma once
// =============================================================================
// ff_kway_merger_pipeline.hpp  —  Merge K-way con Pipeline asincrona (FastFlow)
// =============================================================================
//
// ARCHITETTURA
// ─────────────────────────────────────────────────────────────────────────────
// Questo modulo espone un'unica funzione pubblica:
//
//   ffKwayMergePipeline(runPaths, outputPath, nWorkers, deleteRuns)
//
// che esegue il merge di tutte le run in un unico file ordinato usando:
//
//   LIVELLO 1 (parallelo, ff::ParallelFor):
//     Ogni worker prende un gruppo di run e le fonde con pipelineMergePass()
//     in un file intermedio. Il Worker lancia internamente un std::thread
//     per il Writer. ff::ParallelFor distribuisce il lavoro con work-stealing.
//
//   LIVELLO 2 (singolo thread, pipeline):
//     Gli intermedi vengono fusi con una singola pipelineMergePass() finale.
//
// PERCHÉ ff::ParallelFor INVECE DI ff::Pipeline
// ─────────────────────────────────────────────────────────────────────────────
// Una ff::Pipeline native (ff_node_t per Reader, Merger, Writer) sarebbe la
// soluzione più "FastFlow-idiomatica". Tuttavia:
//
//   1. La Fase 1 (sort dei chunk) usa già una ff::farm con Emitter e Worker.
//      Il runtime FF ha già impostato CPU pinning sui suoi thread.
//
//   2. Se la Fase 2 usa una ff::Pipeline aggiuntiva, FastFlow crea nuovi
//      thread pin-nati e può interferire con quelli della Fase 1.
//
//   3. ff::ParallelFor usa gli stessi worker pool della farm (senza creare
//      nuovi thread) e non ha conflitti di affinità. Il Writer asincrono
//      di pipelineMergePass è un semplice std::thread senza pin.
//
// Questa scelta è consistente con la versione OMP, mantenendo la stessa
// primitiva pipelineMergePass() come unico punto di implementazione del merge.
//
// CONTEGGIO THREAD
// ─────────────────────────────────────────────────────────────────────────────
// ff_sort.cpp usa nWorkers = ff_numCores() - 1 (lascia 1 core all'Emitter).
// Con pipelineMergePass, ogni worker lancia un std::thread aggiuntivo (Writer).
// Thread totali al Livello 1: nWorkers (FF) + nWorkers (Writer) = 2 * nWorkers.
// Assicurarsi che questo non causi over-subscription sul cluster.
//
// =============================================================================

#include "pipeline_merge_pass.hpp"  // pipelineMergePass()
#include "kway_merger.hpp"          // moveOrCopyRun(), mergeVerboseEnabled()

#include <ff/parallel_for.hpp>

#include <atomic>
#include <exception>
#include <string>
#include <vector>


// =============================================================================
// Funzione di supporto per ricavare la directory temporanea
// =============================================================================
inline std::string ffPipelineTmpDir(const std::string& firstRunPath)
{
    size_t pos = firstRunPath.rfind('/');
    return (pos != std::string::npos) ? firstRunPath.substr(0, pos) : ".";
}


// =============================================================================
// ffKwayMergePipeline — Orchestratore del merge in pipeline (FastFlow)
// =============================================================================
//
// Strategia identica a ompKwayMergePipeline ma usa ff::ParallelFor per il
// parallelismo del Livello 1 anziché #pragma omp parallel for.
//
// Questo garantisce che il runtime FastFlow gestisca entrambe le fasi
// (sort e merge) senza conflitti di CPU affinity.
//
// =============================================================================
inline void ffKwayMergePipeline(
    const std::vector<std::string>& runPaths,
    const std::string&              outputPath,
    int                             nWorkers,
    bool                            deleteRuns = true)
{
    if (runPaths.empty()) {
        throw std::runtime_error("ffKwayMergePipeline: nessuna run");
    }
    if (nWorkers < 1) nWorkers = 1;

    const bool verbose = mergeVerboseEnabled();

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] impl=ff-pipeline initialRuns=%zu workers=%d\n",
                     runPaths.size(), nWorkers);
    }

    // ── Caso banale: una sola run ─────────────────────────────────────────────
    if (runPaths.size() == 1) {
        if (verbose) std::fprintf(stderr, "[merge] mode=singleRun\n");
        moveOrCopyRun(runPaths[0], outputPath, deleteRuns);
        return;
    }

    // ── Poche run → merge diretto in pipeline ─────────────────────────────────
    // Come nella versione OMP: se le run sono ≤ 2*nWorkers, il primo livello
    // parallelo creerebbe gruppi da 1-2 run e una seconda passata finale.
    if (runPaths.size() <= 2 * static_cast<size_t>(nWorkers)) {
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=1 runs=%zu mode=directPipeline\n",
                         runPaths.size());
        }
        pipelineMergePass(runPaths, outputPath, deleteRuns);
        return;
    }

    // ── Caso generale: 2 livelli ──────────────────────────────────────────────
    const int workers = std::min(nWorkers, static_cast<int>(runPaths.size()));

    const size_t groupSize = (runPaths.size() + static_cast<size_t>(workers) - 1)
                           / static_cast<size_t>(workers);

    const int groups = static_cast<int>(
        (runPaths.size() + groupSize - 1) / groupSize);

    const std::string tmpDir = ffPipelineTmpDir(runPaths[0]);

    std::vector<std::string> intermediates(groups);
    for (int g = 0; g < groups; ++g) {
        intermediates[g] = tmpDir + "/run_ff_pipe_" + std::to_string(g) + ".bin";
    }

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] level=1 runs=%zu groups=%d groupSize=%zu mode=parallelPipeline\n",
                     runPaths.size(), groups, groupSize);
    }

    // ── LIVELLO 1: ff::ParallelFor ────────────────────────────────────────────
    // Il secondo argomento (false) disabilita il CPU pinning interno di FF,
    // evitando conflitti con il pinning già impostato dalla farm della Fase 1.
    ff::ParallelFor pf(workers, false);
    pf.no_mapping();

    std::atomic<bool>  mergeError{false};
    std::vector<std::exception_ptr> groupErrors(groups);

    pf.parallel_for(0, groups, 1, 0,
        [&](const long g) {
            if (mergeError.load(std::memory_order_relaxed)) return;

            try {
                const size_t begin = static_cast<size_t>(g) * groupSize;
                const size_t end   = std::min(begin + groupSize, runPaths.size());

                std::vector<std::string> group(runPaths.begin() + begin,
                                               runPaths.begin() + end);

                // pipelineMergePass: stessa primitiva della versione OMP.
                pipelineMergePass(group, intermediates[g], deleteRuns);

            } catch (...) {
                mergeError.store(true, std::memory_order_relaxed);
                groupErrors[static_cast<size_t>(g)] = std::current_exception();
            }
        }
    );

    for (const auto& err : groupErrors) {
        if (err) std::rethrow_exception(err);
    }

    // ── LIVELLO 2: merge finale degli intermedi ───────────────────────────────
    if (verbose) {
        std::fprintf(stderr,
                     "[merge] level=2 runs=%d mode=finalPipeline\n", groups);
    }

    pipelineMergePass(intermediates, outputPath, /*deleteSource=*/true);
}
