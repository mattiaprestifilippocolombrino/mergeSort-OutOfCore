#pragma once
// =============================================================================
// omp_kway_merger_multipass_pipeline.hpp  —  K-way Merge ibrido Multipass+Pipeline
// =============================================================================
//
// PERCHÉ ESISTE QUESTO MODULO
// ─────────────────────────────────────────────────────────────────────────────
// Il merge K-way ha due problemi distinti su un cluster HPC:
//
//   1. TROPPI FILE APERTI: Se le run sono centinaia, aprirle tutte insieme
//      supera il limite del sistema operativo (ulimit -n 1024 su Linux).
//      → Soluzione del MULTIPASS: fan-in limitato, più passate.
//
//   2. I/O BLOCCANTE: Durante mergePass() tradizionale, il thread è idle
//      mentre aspetta il disco per ogni fread/fwrite.
//      → Soluzione della PIPELINE: Reader a blocchi grandi + Writer asincrono.
//
// Questo modulo fonde le due soluzioni in un unico algoritmo ottimale:
//   - La struttura MULTIPASS garantisce che il numero di file aperti
//     simultaneamente non superi mai MULTIPASS_OPEN_FILES_SAFE.
//   - La primitiva PIPELINE nasconde la latenza I/O in ogni singola fusione.
//
// ARCHITETTURA
// ─────────────────────────────────────────────────────────────────────────────
//
//   Ogni passata:
//
//   Runs    ┌─────────┐  ┌─────────┐  ┌─────────┐
//   input   │ gruppo 0│  │ gruppo 1│  │ gruppo 2│   ← suddivisione per fan-in
//           └────┬────┘  └────┬────┘  └────┬────┘
//                │            │            │
//         [pipelineMergePass per ogni gruppo, in parallelo con OMP]
//                │            │            │
//           intermedio_0  intermedio_1  intermedio_2
//
//   Ogni pipelineMergePass() usa internamente:
//     RunBlockReader (4MB/run)  →  Min-Heap in RAM  →  Writer asincrono (32MB)
//
//   La passata successiva usa gli intermedi come nuove run. Quando ne rimangono
//   ≤ mergeFan, una sola pipelineMergePass() finale completa il lavoro.
//
// GARANZIA SUL NUMERO DI FILE APERTI
// ─────────────────────────────────────────────────────────────────────────────
//   safeWorkers  = min(nThreads, MULTIPASS_OPEN_FILES_SAFE / (mergeFan + 1))
//   FD aperti    = safeWorkers × (mergeFan + 1)  ≤  MULTIPASS_OPEN_FILES_SAFE
//
//   Esempio concreto (mergeFan=32, nThreads=16):
//     safeWorkers  = min(16, 512 / 33) = min(16, 15) = 15
//     FD aperti    = 15 × 33 = 495  ← ben sotto il limite di 1024
//
// NUMERO DI PASSATE SU DISCO
// ─────────────────────────────────────────────────────────────────────────────
//   R ≤ mergeFan (es. 30 run):         1 passata  → lettura+scrittura una sola volta
//   R ≤ mergeFan² (es. ≤ 1024 run):    2 passate  → tipico su 32GB RAM / chunk 64MB
//   R ≤ mergeFan³ (es. ≤ 32768 run):   3 passate  → raro, chunk da ~1MB
//
//   In tutti i casi, ogni passata usa I/O asincrono: il numero di passate è
//   l'unico overhead aggiuntivo rispetto a un merge ideale a passata singola.
//
// =============================================================================

#include "pipeline_merge_pass.hpp"  // pipelineMergePass() — la primitiva I/O asincrona
#include "kway_merger.hpp"          // moveOrCopyRun(), mergeVerboseEnabled()

#include <omp.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <string>
#include <vector>


// =============================================================================
// Costanti di tuning — best practice per cluster HPC
// =============================================================================

// Fan-in di default per ogni singola fusione.
//
// Scelta: 32 è il punto di equilibrio tra:
//   - Uso RAM:     ogni RunBlockReader alloca READ_BLOCK_SIZE (4MB) di buffer.
//                  Con 32 run: 32 × 4MB = 128MB per merge task → gestibile.
//   - Passate:     con mergeFan=32, R=1024 basta un solo livello intermedio.
//   - Heap:        una min-heap su 32 elementi è O(log 32 ≈ 5) per estrazione.
//                  Trascurabile rispetto al costo I/O.
static constexpr int MULTIPASS_MERGE_FAN_DEFAULT = 32;

// Limite conservativo di file aperti per l'intero processo.
//
// Linux default: ulimit -n 1024.
// Usiamo 512 = metà del limite, per lasciare margine a:
//   stdin (0), stdout (1), stderr (2), file input principale, file output,
//   eventuali file di log, handle interni della libc e del runtime OMP.
static constexpr int MULTIPASS_OPEN_FILES_SAFE = 512;


// =============================================================================
// safeConcurrentWorkers — Calcola il numero sicuro di merge in parallelo
// =============================================================================
//
// Ogni pipelineMergePass() apre:
//   - `mergeFan` file di input (uno per run nel gruppo)
//   - 1 file di output (il file intermedio prodotto)
//   totale: mergeFan + 1 file descriptor per worker
//
// Con `workers` thread attivi contemporaneamente:
//   FD totali = workers × (mergeFan + 1)
//
// Vogliamo:
//   workers × (mergeFan + 1) ≤ MULTIPASS_OPEN_FILES_SAFE
//   → workers ≤ MULTIPASS_OPEN_FILES_SAFE / (mergeFan + 1)
//
// In pratica prendiamo il minimo tra questo limite e i thread OMP disponibili.
// =============================================================================
inline int safeConcurrentWorkers(int mergeFan, int availableThreads)
{
    if (mergeFan < 1)          mergeFan         = 1;
    if (availableThreads < 1)  availableThreads = 1;
    const int maxSafe = MULTIPASS_OPEN_FILES_SAFE / (mergeFan + 1);
    return std::max(1, std::min(availableThreads, maxSafe));
}


// =============================================================================
// Helper: ricava la directory temporanea dal path della prima run
// =============================================================================
inline std::string multipassPipeTmpDir(const std::string& firstRunPath)
{
    const size_t pos = firstRunPath.rfind('/');
    return (pos != std::string::npos) ? firstRunPath.substr(0, pos) : ".";
}


// =============================================================================
// ompKwayMergeMultipassPipeline — Orchestratore ibrido Multipass + Pipeline
// =============================================================================
//
// Parametri:
//   runPaths   - Le run ordinate prodotte dalla Fase 1 (sortToRuns).
//   outputPath - Il file di output finale ordinato.
//   mergeFan   - Fan-in massimo (default 32). Controlla RAM e FD per merge task.
//   deleteRuns - Se true, rimuove i file sorgente dopo la fusione.
//
// =============================================================================
inline void ompKwayMergeMultipassPipeline(
    const std::vector<std::string>& runPaths,
    const std::string&              outputPath,
    int                             mergeFan   = MULTIPASS_MERGE_FAN_DEFAULT,
    bool                            deleteRuns = true)
{
    if (runPaths.empty()) {
        throw std::runtime_error("ompKwayMergeMultipassPipeline: nessuna run");
    }
    if (mergeFan < 2) mergeFan = 2;

    const bool verbose     = mergeVerboseEnabled();
    const int  nThreads    = std::max(1, omp_get_max_threads());

    // Numero di merge paralleli sicuri.
    // È il min tra i thread disponibili e il massimo consentito dai file descriptor.
    const int  safeWorkers = safeConcurrentWorkers(mergeFan, nThreads);

    const std::string tmpDir = multipassPipeTmpDir(runPaths[0]);

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] impl=multipass-pipeline initialRuns=%zu"
                     " mergeFan=%d safeWorkers=%d (di %d) maxFDaSimult=%d\n",
                     runPaths.size(), mergeFan, safeWorkers, nThreads,
                     safeWorkers * (mergeFan + 1));
    }

    // ── Caso banale: una sola run ─────────────────────────────────────────────
    // Nessun merge da fare: spostamento/copia O(1) del singolo file.
    if (runPaths.size() == 1) {
        if (verbose) std::fprintf(stderr, "[merge] pass=1/1 runs=1 mode=singleRun\n");
        moveOrCopyRun(runPaths[0], outputPath, deleteRuns);
        return;
    }

    // ── Caso ottimale: tutte le run entrano in un singolo merge ───────────────
    // Una sola pipelineMergePass(): 1 passata su disco, Writer asincrono attivo.
    // Nessun file intermedio, nessun overhead di loop.
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
    // currentLevel: le run da ridurre nella passata corrente.
    // Inizia con le run originali; ad ogni passata diventa il vettore dei file
    // intermedi prodotti, finché non ne rimane uno solo (o ≤ mergeFan).
    //
    // PUNTO FORTE MULTIPASS: il while gestisce R arbitrariamente grande
    // senza mai superare il budget di file descriptor.
    std::vector<std::string> currentLevel = runPaths;
    int pass = 0;

    while (static_cast<int>(currentLevel.size()) > mergeFan) {
        ++pass;
        const int R         = static_cast<int>(currentLevel.size());
        const int numGroups = (R + mergeFan - 1) / mergeFan;

        // Stima del numero di passate totali (solo per il log, non bloccante).
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] pass=%d runs=%d groups=%d activeWorkers=%d"
                         " mode=parallelPipelinePass\n",
                         pass, R, numGroups, std::min(safeWorkers, numGroups));
        }

        // Prepara i nomi dei file intermedi per questa passata.
        // Il numero di passata nel nome garantisce che nomi di passate diverse
        // non collidano mai, anche se il processo viene eseguito in modo asincrono.
        std::vector<std::string> nextLevel(numGroups);
        for (int g = 0; g < numGroups; ++g) {
            nextLevel[g] = tmpDir + "/run_mpp_p" + std::to_string(pass)
                         + "_g" + std::to_string(g) + ".bin";
        }

        // Politica di cancellazione delle sorgenti:
        //   Pass 1: cancella le run originali solo se deleteRuns è true.
        //   Pass > 1: i file in currentLevel sono intermedi creati da noi → cancella sempre.
        const bool delSrc = (pass > 1) || deleteRuns;

        // Strutture per la propagazione sicura degli errori attraverso i thread OMP.
        std::atomic<bool>  hadError{false};
        std::exception_ptr firstError;
        std::mutex         errorMutex;

        // ── Loop parallelo: ogni gruppo viene fuso con pipelineMergePass() ───
        //
        // num_threads(safeWorkers): mai più di safeWorkers thread attivi in
        //   contemporanea → FD totali ≤ MULTIPASS_OPEN_FILES_SAFE. Garantito.
        //
        // schedule(dynamic): i gruppi vengono assegnati ai thread man mano che
        //   si liberano. Utile quando i gruppi hanno dimensioni diverse (l'ultimo
        //   gruppo spesso ha meno di mergeFan run).
        //
        // PUNTO FORTE PIPELINE: ogni pipelineMergePass() usa internamente:
        //   - RunBlockReader: lettura a blocchi da 4MB per run (poche syscall)
        //   - Min-Heap in RAM: merge K-way senza I/O nel loop principale
        //   - Writer std::thread: scrittura su disco in parallelo con il merge
        //   Questo nasconde completamente la latenza I/O dell'output.
        #pragma omp parallel for schedule(dynamic) num_threads(safeWorkers) \
            default(none) \
            shared(currentLevel, nextLevel, R, numGroups, mergeFan, delSrc, \
                   hadError, firstError, errorMutex)
        for (int g = 0; g < numGroups; ++g) {
            // Fast exit se un altro gruppo ha già fallito.
            if (hadError.load(std::memory_order_relaxed)) continue;

            const int begin = g * mergeFan;
            const int end   = std::min(begin + mergeFan, R);

            // Costruisce il sottovettore per il gruppo g: al massimo mergeFan elementi.
            std::vector<std::string> group(
                currentLevel.begin() + begin,
                currentLevel.begin() + end);

            try {
                pipelineMergePass(group, nextLevel[g], delSrc);
            } catch (...) {
                hadError.store(true, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(errorMutex);
                if (!firstError) firstError = std::current_exception();
            }
        }

        // Propaga la prima eccezione ricevuta dai thread OMP.
        if (firstError) std::rethrow_exception(firstError);

        // La passata successiva parte dagli intermedi appena prodotti.
        currentLevel = std::move(nextLevel);
    }

    // ── Passata finale: currentLevel.size() ≤ mergeFan ───────────────────────
    //
    // A questo punto rimangono al massimo mergeFan file. Una singola
    // pipelineMergePass() li fonde direttamente in outputPath:
    //   - Nessun file intermedio aggiuntivo.
    //   - Writer asincrono attivo: il thread principale non aspetta il disco.
    //   - Un solo std::thread extra (il Writer): nessun overhead OMP.
    ++pass;
    if (verbose) {
        std::fprintf(stderr,
                     "[merge] pass=%d runs=%zu mode=finalPipeline (→ output)\n",
                     pass, currentLevel.size());
    }

    // I file in currentLevel dopo pass > 1 sono sempre intermedi: cancella sempre.
    const bool finalDelSrc = (pass > 1) || deleteRuns;
    pipelineMergePass(currentLevel, outputPath, finalDelSrc);
}
