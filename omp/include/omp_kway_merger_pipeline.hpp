#pragma once
// =============================================================================
// omp_kway_merger_pipeline.hpp  —  Merge K-way con Pipeline asincrona (OpenMP)
// =============================================================================
//
// ARCHITETTURA
// ─────────────────────────────────────────────────────────────────────────────
// Questo modulo espone un'unica funzione pubblica:
//
//   ompKwayMergePipeline(runPaths, outputPath, deleteRuns)
//
// che esegue il merge di tutte le run in un unico file ordinato usando la
// Pipeline a 3 stadi definita in pipeline_merge_pass.hpp:
//
//   RunBlockReader (lettura a blocchi)  →  Merger (min-heap in RAM)
//                                       →  Writer (fwrite a blocchi 32MB)
//
// COME SI INSERISCE NELL'ARCHITETTURA OMP
// ─────────────────────────────────────────────────────────────────────────────
// La Fase 1 (sort dei chunk) usa #pragma omp parallel for / task.
// La Fase 2 (questo modulo) usa std::thread per il Writer, indipendente da OMP.
//
// Questo evita il problema della versione "flat": non si crea una seconda
// passata su disco con file intermedi. I dati vengono letti e scritti UNA SOLA
// VOLTA, con la latenza I/O nascosta dal pipeline.
//
// CONFRONTO CON LE ALTRE IMPLEMENTAZIONI OMP
// ─────────────────────────────────────────────────────────────────────────────
//
//   flat (default):     2 passate su disco, stage 2 seriale. Scala male.
//   legacy (multi-pass): N passate, parallelismo sui gruppi. Inefficiente per K piccolo.
//   pipeline (questo):   1 passata, Writer asincrono. Ottimale su I/O-bound.
//
// =============================================================================

#include "pipeline_merge_pass.hpp"  // pipelineMergePass(), DoubleBuffer, WriteBuffer
#include "kway_merger.hpp"          // moveOrCopyRun(), mergeVerboseEnabled()

#include <omp.h>

#include <algorithm>
#include <string>
#include <vector>


// =============================================================================
// Funzione di supporto per ricavare la directory temporanea
// =============================================================================
inline std::string ompPipelineTmpDir(const std::string& firstRunPath)
{
    size_t pos = firstRunPath.rfind('/');
    return (pos != std::string::npos) ? firstRunPath.substr(0, pos) : ".";
}


// =============================================================================
// ompKwayMergePipeline — Orchestratore del merge in pipeline (OpenMP)
// =============================================================================
//
// Strategia:
//   Se le run sono abbastanza da giustificare un primo livello parallelo
//   (ossia più run dei thread disponibili), questa funzione:
//
//   LIVELLO 1 (parallelo, OMP parallel for):
//     Ogni thread prende un gruppo di run e le fonde con pipelineMergePass()
//     in un file intermedio privato. Questa fase usa il parallelismo del disco
//     (letture da file distinti in parallelo) + la pipeline Writer asincrona.
//
//   LIVELLO 2 (singolo thread, pipeline):
//     Gli intermedi vengono fusi in un'unica pipelineMergePass() finale.
//     Una sola passata su disco. Il Writer scrive in blocchi da 32MB.
//
//   Se le run sono poche (< 2 * nThread), si esegue direttamente un singolo
//   pipelineMergePass() su tutte le run: 1 sola passata, 0 overhead.
//
// NOTA SUI THREAD:
//   pipelineMergePass() lancia internamente 1 std::thread per il Writer.
//   Con T thread OMP in Livello 1, i thread totali sono T (OMP) + T (Writer) = 2T.
//   Tenere presente quando si imposta --threads sul cluster.
//
// =============================================================================
inline void ompKwayMergePipeline(
    const std::vector<std::string>& runPaths,
    const std::string&              outputPath,
    bool                            deleteRuns = true)
{
    if (runPaths.empty()) {
        throw std::runtime_error("ompKwayMergePipeline: nessuna run");
    }

    const bool verbose  = mergeVerboseEnabled();
    const int  nThreads = std::max(1, omp_get_max_threads());

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] impl=omp-pipeline initialRuns=%zu threads=%d\n",
                     runPaths.size(), nThreads);
    }

    // ── Caso banale: una sola run ─────────────────────────────────────────────
    if (runPaths.size() == 1) {
        if (verbose) std::fprintf(stderr, "[merge] mode=singleRun\n");
        moveOrCopyRun(runPaths[0], outputPath, deleteRuns);
        return;
    }

    // ── Caso: poche run → merge diretto in pipeline ───────────────────────────
    // Con runPaths.size() <= nThreads un primo livello parallelo produrrebbe
    // gruppi da 1 run sola ciascuno, che è inutile. Eseguiamo direttamente
    // un merge singolo in pipeline: 1 passata, Writer asincrono.
    if (runPaths.size() <= static_cast<size_t>(nThreads)) {
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=1 runs=%zu mode=directPipeline\n",
                         runPaths.size());
        }
        pipelineMergePass(runPaths, outputPath, deleteRuns);
        return;
    }

    // ── Caso generale: molte run → 2 livelli ──────────────────────────────────
    //
    //   LIVELLO 1: T thread OMP, ognuno fonde (runPaths.size() / T) run
    //              in un file intermedio con pipelineMergePass().
    //              I thread lavorano su set distinti di run: no contesa I/O.
    //
    //   LIVELLO 2: unica pipelineMergePass() sugli T file intermedi.
    //              Merge finale single-thread con Writer asincrono.

    const size_t groupSize = (runPaths.size() + static_cast<size_t>(nThreads) - 1)
                           / static_cast<size_t>(nThreads);

    const int groups = static_cast<int>(
        (runPaths.size() + groupSize - 1) / groupSize);

    const std::string tmpDir = ompPipelineTmpDir(runPaths[0]);

    // Pre-calcola i nomi dei file intermedi (uno per gruppo).
    std::vector<std::string> intermediates(groups);
    for (int g = 0; g < groups; ++g) {
        intermediates[g] = tmpDir + "/run_omp_pipe_" + std::to_string(g) + ".bin";
    }

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] level=1 runs=%zu groups=%d groupSize=%zu mode=parallelPipeline\n",
                     runPaths.size(), groups, groupSize);
    }

    // ── LIVELLO 1: merge parallelo in pipeline ────────────────────────────────
    std::atomic<bool>  mergeError{false};
    std::exception_ptr firstError;

    #pragma omp parallel for schedule(static) default(none) \
        shared(runPaths, intermediates, groupSize, groups, deleteRuns, \
               mergeError, firstError)
    for (int g = 0; g < groups; ++g) {
        if (mergeError.load(std::memory_order_relaxed)) continue;

        try {
            const size_t begin = static_cast<size_t>(g) * groupSize;
            const size_t end   = std::min(begin + groupSize, runPaths.size());

            std::vector<std::string> group(runPaths.begin() + begin,
                                           runPaths.begin() + end);

            // pipelineMergePass: lettura a blocchi + Writer asincrono.
            // Ogni thread ha il proprio writer thread → no contesa.
            pipelineMergePass(group, intermediates[g], deleteRuns);

        } catch (...) {
            mergeError.store(true, std::memory_order_relaxed);
            #pragma omp critical(omp_pipeline_error)
            {
                if (!firstError) firstError = std::current_exception();
            }
        }
    }

    if (firstError) std::rethrow_exception(firstError);

    // ── LIVELLO 2: merge finale degli intermedi ───────────────────────────────
    if (verbose) {
        std::fprintf(stderr,
                     "[merge] level=2 runs=%d mode=finalPipeline\n", groups);
    }

    // Unica pipelineMergePass sugli intermedi: 1 sola passata, Writer asincrono.
    // deleteSource=true: elimina automaticamente i file intermedi.
    pipelineMergePass(intermediates, outputPath, /*deleteSource=*/true);
}
