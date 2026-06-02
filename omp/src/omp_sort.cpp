// =============================================================================
// omp_sort.cpp  —  MergeSort out-of-core con OpenMP
// =============================================================================
//
// Utilizzo:
//   ./omp_sort <input> <output> [opzioni]
//
//   --chunk-mb     N     Dimensione del blocco in RAM per ogni run (default: 256 MB)
//   --threads      N     Numero di thread OpenMP (default: max hw)
//   --tmp-dir      PATH  Directory per i file temporanei (default: /tmp)
//   --merge-fan    N     Fan-in massimo solo per --legacy-merge (default: 64)
//   --legacy-merge       Usa il vecchio merge multi-pass a livelli
//   --pipeline-merge     Usa il merge in pipeline I/O asincrona (raccomandato)
//   --no-par-merge       Disabilita merge parallelo
//   --keep-runs          Non eliminare i file di run dopo il merge (debug)
//
// ─────────────────────────────────────────────────────────────────────────────
// FLUSSO IN DUE FASI
// ─────────────────────────────────────────────────────────────────────────────
//
// FASE 1  —  sort_to_runs()   [chunk_sorter.hpp]
//   Il thread principale legge il file input a chunk da --chunk-mb MB.
//   Ogni chunk viene ceduto a un OpenMP task che lo ordina per chiave
//   (std::sort sull'indice leggero di RecordIndex, zero-copy dei payload)
//   e lo scrive come file di run ordinata in tmp-dir.
//
//   run_0.bin, run_1.bin, run_2.bin, ...  ← ognuno già ordinato
//
// FASE 2  —  selezione dell'algoritmo di merge [tre implementazioni]:
//
//   A) --legacy-merge    [omp_kway_merger.hpp]          — multi-pass a livelli (storico)
//   B) (default)         [omp_kway_merger.hpp]          — flat a due stadi (parallelo + seriale)
//   C) --pipeline-merge  [omp_kway_merger_pipeline.hpp] — pipeline I/O asincrona (raccomandato)
//
//   La versione pipeline nasconde la latenza I/O: il Writer scrive su disco
//   mentre il Merger lavora in RAM. Unica passata sui dati.
//
// ─────────────────────────────────────────────────────────────────────────────
// PARALLELISMO
// ─────────────────────────────────────────────────────────────────────────────
//   Fase 1: ogni chunk è ordinato in un task OMP → parallelismo CPU.
//   Fase 2: i thread fondono blocchi disgiunti di run; poi il main thread
//           esegue il merge finale degli intermedi. Il numero di passate su
//           disco e' ridotto al minimo, utile su /tmp locale dei nodi HPC.
//
// =============================================================================

#include "chunk_sorter.hpp"               // sort_to_runs()
#include "omp_kway_merger.hpp"            // ompKwayMerge(), ompKwayMergeLegacy()
#include "omp_kway_merger_pipeline.hpp"  // ompKwayMergePipeline() — I/O asincrono
#include "temp_dir.hpp"

#include <iostream>
#include <string>
#include <chrono>
#include <cstdlib>
#include <omp.h>

// Restituisce la durata in secondi tra due time_point.
static double seconds(std::chrono::steady_clock::time_point a,
                      std::chrono::steady_clock::time_point b) {
    std::chrono::duration<double> elapsed = b - a;
    return elapsed.count();
}

static void usage(const char* prog) {
    std::cerr << "Utilizzo: " << prog << " <input> <output> [opzioni]\n"
              << "  --chunk-mb     N     Dimensione chunk in MB      (default: 256)\n"
              << "  --threads      N     Thread OpenMP               (default: max hw)\n"
              << "  --tmp-dir      PATH  Directory file temporanei   (default: /tmp)\n"
              << "  --merge-fan    N     Fan-in solo legacy merge    (default: 64)\n"
              << "  --legacy-merge       Usa il vecchio merge multi-pass a livelli\n"
              << "  --pipeline-merge     Pipeline I/O asincrona — raccomandato\n"
              << "  --no-par-merge       Disabilita merge parallelo (solo flat/legacy)\n"
              << "  --keep-runs          Non eliminare le run (debug)\n";
    std::exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        usage(argv[0]);
    }

    // Parametri di default:
    // chunk grande abbastanza da sfruttare std::sort su blocchi corposi.
    // Il nuovo merge OpenMP non usa merge_fan: divide le run tra i thread e
    // fa una sola passata finale. mergeFan resta solo per --legacy-merge.
    std::string inputPath  = argv[1];
    std::string outputPath = argv[2];
    std::string tmpDir     = "/tmp";
    size_t      chunkMb    = 256;
    int         nThreads    = omp_get_max_threads();
    int         mergeFan   = 64;
    bool        keepRuns        = false;
    bool        parallelMerge   = true;
    bool        legacyMerge     = false;
    bool        pipelineMerge   = false;  // --pipeline-merge: I/O asincrona

    // Parsing semplice e diretto delle opzioni.
    // Mantengo nomi e parametri uguali tra versioni OMP, FF e MPI: rende piu'
    // facile confrontare i benchmark.
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];

        if (a == "--chunk-mb" && i + 1 < argc) {
            chunkMb = std::stoul(argv[++i]);
        } else if (a == "--threads" && i + 1 < argc) {
            nThreads = std::stoi(argv[++i]);
        } else if (a == "--tmp-dir" && i + 1 < argc) {
            tmpDir = argv[++i];
        } else if (a == "--merge-fan" && i + 1 < argc) {
            mergeFan = std::stoi(argv[++i]);
        } else if (a == "--legacy-merge") {
            legacyMerge   = true;
            pipelineMerge = false;
        } else if (a == "--pipeline-merge") {
            pipelineMerge = true;
            legacyMerge   = false;
        } else if (a == "--no-par-merge") {
            parallelMerge = false;
        } else if (a == "--keep-runs") {
            keepRuns = true;
        } else {
            usage(argv[0]);
        }
    }

    if (chunkMb == 0) {
        std::cerr << "[WARN] --chunk-mb 0 non valido, imposto a 1\n";
        chunkMb = 1;
    }

    omp_set_num_threads(nThreads);

    // Converto chunk_mb in byte (1 MB = 1024 * 1024 byte).
    const size_t chunkBytes = chunkMb * 1024ULL * 1024ULL;

    // Ogni esecuzione usa una sottodirectory temporanea unica.
    // Se keep_runs=false viene cancellata automaticamente dal distruttore.
    TempDir workTmp(tmpDir, "spm_omp", keepRuns);

    // Determina la stringa descrittiva dell'implementazione di merge scelta.
    const char* mergeImplName = pipelineMerge ? "pipeline async I/O" :
                                legacyMerge   ? "legacy multi-pass"  :
                                                "flat two-stage";

    std::cout << "=== OMP MergeSort out-of-core ===\n"
              << "  input        : " << inputPath        << "\n"
              << "  output       : " << outputPath       << "\n"
              << "  chunk        : " << chunkMb          << " MB\n"
              << "  threads      : " << nThreads          << "\n"
              << "  merge impl   : " << mergeImplName    << "\n"
              << "  merge fan-in : " << (legacyMerge ? std::to_string(mergeFan) : "non usato") << "\n"
              << "  merge paral. : " << (parallelMerge ? "si" : "no") << "\n"
              << "  tmp          : " << workTmp.str()    << "\n"
              << "  PAYLOAD_MAX  : " << PAYLOAD_MAX       << " B\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // FASE 1: sort parallelo dei chunk → file di run
    // ─────────────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    // sort_to_runs e' il cuore della fase 1:
    // produce tanti file temporanei ordinati, uno per chunk.
    std::vector<std::string> runs = sortToRuns(inputPath, workTmp.str(), chunkBytes);

    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    std::cout << "Fase 1 (sort): " << runs.size() << " run create in "
              << seconds(t0, t1) << " s\n";

    if (runs.empty()) {
        std::cout << "File vuoto — output non creato.\n";
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // FASE 2: K-way merge → file di output
    // ─────────────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();

    // Il merge lavora solo su file ordinati: non carica mai tutte le run in RAM.
    if (pipelineMerge) {
        // Pipeline I/O asincrona: Reader a blocchi + Merger in RAM + Writer asincrono.
        // Unica passata sui dati: volume I/O identico al single-thread.
        ompKwayMergePipeline(runs, outputPath, /*deleteRuns=*/!keepRuns);
    } else if (legacyMerge) {
        // Multi-pass a livelli con task OMP (storico).
        ompKwayMergeLegacy(runs, outputPath, mergeFan, /*deleteRuns=*/!keepRuns,
                           /*parallelMerge=*/parallelMerge);
    } else {
        // Flat a due stadi: stage 1 parallelo, stage 2 seriale (default).
        ompKwayMerge(runs, outputPath, /*deleteRuns=*/!keepRuns,
                     /*parallelMerge=*/parallelMerge);
    }

    std::chrono::steady_clock::time_point t3 = std::chrono::steady_clock::now();

    // ─────────────────────────────────────────────────────────────────────────
    // Report finale
    // ─────────────────────────────────────────────────────────────────────────
    const double ts = seconds(t0, t1); // sort
    const double tm = seconds(t2, t3); // merge
    const double tt = seconds(t0, t3); // totale

    std::cout << "Fase 2 (merge): " << tm << " s\n\n"
              << "--- Riepilogo tempi ---\n"
              << "  Sort parallelo (Fase 1) : " << ts << " s\n"
              << "  K-way merge   (Fase 2) : " << tm << " s\n"
              << "  Totale                 : " << tt << " s\n";

    return 0;
}
