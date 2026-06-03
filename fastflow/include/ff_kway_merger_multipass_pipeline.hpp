#pragma once
// =============================================================================
// ff_kway_merger_multipass_pipeline.hpp  —  K-way Merge ibrido Multipass+Pipeline (FF)
// =============================================================================
//
// PERCHÉ ESISTE QUESTO MODULO
// ─────────────────────────────────────────────────────────────────────────────
// Versione FastFlow del merge ibrido Multipass + Pipeline.
// La logica è identica a omp_kway_merger_multipass_pipeline.hpp; cambia solo
// il meccanismo di parallelismo: ff::ParallelFor al posto di #pragma omp parallel for.
//
// PERCHÉ ff::ParallelFor E NON ff::Pipeline NATIVA
// ─────────────────────────────────────────────────────────────────────────────
// Una ff::Pipeline nativa (nodi Reader, Merger, Writer come ff_node_t) sarebbe
// la soluzione più "FastFlow-idiomatica". Tuttavia:
//
//   1. La Fase 1 (sort dei chunk) usa già una ff::farm con Emitter e Worker.
//      Il runtime FF ha già eseguito il CPU pinning sui suoi thread worker.
//
//   2. Una nuova ff::Pipeline crea nuovi thread pin-nati che possono collidere
//      con quelli della farm della Fase 1 → possibile degrado di prestazioni.
//
//   3. ff::ParallelFor usa un pool di worker separato e leggero, senza pin.
//      Il Writer asincrono di pipelineMergePass è un semplice std::thread
//      senza pin, compatibile con qualsiasi topologia FastFlow.
//
// La primitiva pipelineMergePass() è condivisa con la versione OMP: un solo
// punto di implementazione del cuore del merge.
//
// ARCHITETTURA
// ─────────────────────────────────────────────────────────────────────────────
// Identica a omp_kway_merger_multipass_pipeline.hpp:
//   - Loop multipass con ff::ParallelFor per i gruppi
//   - pipelineMergePass() come motore I/O asincrono per ogni gruppo
//   - Garanzia FD: safeWorkers × (mergeFan + 1) ≤ FF_MULTIPASS_OPEN_FILES_SAFE
//
// =============================================================================

#include "pipeline_merge_pass.hpp"  // pipelineMergePass() — la primitiva I/O asincrona
#include "kway_merger.hpp"          // moveOrCopyRun(), mergeVerboseEnabled()

#include <ff/parallel_for.hpp>

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <string>
#include <vector>


// =============================================================================
// Costanti di tuning — identiche alla versione OMP per coerenza
// =============================================================================

static constexpr int FF_MULTIPASS_MERGE_FAN_DEFAULT = 32;
static constexpr int FF_MULTIPASS_OPEN_FILES_SAFE   = 512;


// =============================================================================
// ffSafeConcurrentWorkers — Numero sicuro di merge paralleli per FF
// =============================================================================
inline int ffSafeConcurrentWorkers(int mergeFan, int availableWorkers)
{
    if (mergeFan < 1)         mergeFan         = 1;
    if (availableWorkers < 1) availableWorkers = 1;
    const int maxSafe = FF_MULTIPASS_OPEN_FILES_SAFE / (mergeFan + 1);
    return std::max(1, std::min(availableWorkers, maxSafe));
}

// Helper: ricava la directory temporanea dal path della prima run.
inline std::string ffMultipassPipeTmpDir(const std::string& firstRunPath)
{
    const size_t pos = firstRunPath.rfind('/');
    return (pos != std::string::npos) ? firstRunPath.substr(0, pos) : ".";
}


// =============================================================================
// ffKwayMergeMultipassPipeline — Orchestratore ibrido Multipass + Pipeline (FF)
// =============================================================================
//
// Parametri:
//   runPaths   - Le run ordinate prodotte dalla Fase 1.
//   outputPath - Il file di output finale ordinato.
//   nWorkers   - Worker FastFlow disponibili (da ff_numCores() o --workers).
//   mergeFan   - Fan-in massimo per ogni pipelineMergePass() (default 32).
//   deleteRuns - Se true, rimuove i file sorgente dopo la fusione.
//
// =============================================================================
inline void ffKwayMergeMultipassPipeline(
    const std::vector<std::string>& runPaths,
    const std::string&              outputPath,
    int                             nWorkers,
    int                             mergeFan   = FF_MULTIPASS_MERGE_FAN_DEFAULT,
    bool                            deleteRuns = true)
{
    if (runPaths.empty()) {
        throw std::runtime_error("ffKwayMergeMultipassPipeline: nessuna run");
    }
    if (nWorkers < 1)  nWorkers  = 1;
    if (mergeFan < 2)  mergeFan  = 2;

    const bool verbose     = mergeVerboseEnabled();

    // Numero di merge paralleli sicuri rispetto al budget di file descriptor.
    const int  safeWorkers = ffSafeConcurrentWorkers(mergeFan, nWorkers);

    const std::string tmpDir = ffMultipassPipeTmpDir(runPaths[0]);

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] impl=ff-multipass-pipeline initialRuns=%zu"
                     " mergeFan=%d safeWorkers=%d (di %d) maxFDaSimult=%d\n",
                     runPaths.size(), mergeFan, safeWorkers, nWorkers,
                     safeWorkers * (mergeFan + 1));
    }

    // ── Caso banale: una sola run ─────────────────────────────────────────────
    if (runPaths.size() == 1) {
        if (verbose) std::fprintf(stderr, "[merge] pass=1/1 runs=1 mode=singleRun\n");
        moveOrCopyRun(runPaths[0], outputPath, deleteRuns);
        return;
    }

    // ── Caso ottimale: tutte le run entrano in un singolo merge ───────────────
    // 1 sola pipelineMergePass(): 1 passata su disco, zero file intermedi.
    if (static_cast<int>(runPaths.size()) <= mergeFan) {
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] pass=1/1 runs=%zu mode=directPipeline (single-pass ottimale)\n",
                         runPaths.size());
        }
        pipelineMergePass(runPaths, outputPath, deleteRuns);
        return;
    }

    // ── Caso generale: passate multiple ──────────────────────────────────────
    //
    // Il loop itera finché il numero di run correnti non scende a ≤ mergeFan.
    // La struttura è identica alla versione OMP: cambia solo il parallelismo.
    std::vector<std::string> currentLevel = runPaths;
    int pass = 0;

    while (static_cast<int>(currentLevel.size()) > mergeFan) {
        ++pass;
        const int R         = static_cast<int>(currentLevel.size());
        const int numGroups = (R + mergeFan - 1) / mergeFan;

        if (verbose) {
            std::fprintf(stderr,
                         "[merge] pass=%d runs=%d groups=%d activeWorkers=%d"
                         " mode=ffParallelPipelinePass\n",
                         pass, R, numGroups, std::min(safeWorkers, numGroups));
        }

        // Prepara i file intermedi per questa passata.
        std::vector<std::string> nextLevel(numGroups);
        for (int g = 0; g < numGroups; ++g) {
            nextLevel[g] = tmpDir + "/run_ffmpp_p" + std::to_string(pass)
                         + "_g" + std::to_string(g) + ".bin";
        }

        const bool delSrc = (pass > 1) || deleteRuns;

        // Strutture per la propagazione sicura degli errori tra i worker FF.
        std::atomic<bool>  hadError{false};
        std::exception_ptr firstError;
        std::mutex         errorMutex;

        // ── ff::ParallelFor sui gruppi ────────────────────────────────────────
        //
        // Il secondo argomento (false) disabilita il CPU pinning interno di FF.
        // Questo evita conflitti di affinità con il pinning già impostato dalla
        // farm della Fase 1 (sort dei chunk).
        //
        // safeWorkers garantisce che il numero totale di file aperti in
        // contemporanea non superi FF_MULTIPASS_OPEN_FILES_SAFE.
        ff::ParallelFor pf(safeWorkers, false);

        pf.parallel_for(0, numGroups, 1, 0,
            [&](const long g) {
                if (hadError.load(std::memory_order_relaxed)) return;

                const int begin = static_cast<int>(g) * mergeFan;
                const int end   = std::min(begin + mergeFan, R);

                // Sottovettore di run per il gruppo g.
                std::vector<std::string> group(
                    currentLevel.begin() + begin,
                    currentLevel.begin() + end);

                try {
                    // PUNTO FORTE PIPELINE: lettura a blocchi + Writer asincrono.
                    // Il worker FF non aspetta mai il disco: la latenza I/O
                    // è nascosta dal thread Writer interno a pipelineMergePass().
                    pipelineMergePass(group, nextLevel[static_cast<size_t>(g)], delSrc);
                } catch (...) {
                    hadError.store(true, std::memory_order_relaxed);
                    std::lock_guard<std::mutex> lk(errorMutex);
                    if (!firstError) firstError = std::current_exception();
                }
            }
        );

        if (firstError) std::rethrow_exception(firstError);

        currentLevel = std::move(nextLevel);
    }

    // ── Passata finale: currentLevel.size() ≤ mergeFan ───────────────────────
    // Un'unica pipelineMergePass() fonde i file rimasti direttamente in outputPath.
    ++pass;
    if (verbose) {
        std::fprintf(stderr,
                     "[merge] pass=%d runs=%zu mode=finalPipeline (→ output)\n",
                     pass, currentLevel.size());
    }

    const bool finalDelSrc = (pass > 1) || deleteRuns;
    pipelineMergePass(currentLevel, outputPath, finalDelSrc);
}
