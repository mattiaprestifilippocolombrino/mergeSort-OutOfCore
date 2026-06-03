// =============================================================================
// ff_sort.cpp  —  MergeSort out-of-core con FastFlow
// =============================================================================
//
// Utilizzo:
//   ./ff_sort <input> <output> [opzioni]
//
//   --chunk-mb  N     Dimensione del blocco in RAM per ogni run (default: 256 MB)
//   --workers   N     Numero di Worker FastFlow (default: max hw - 1)
//   --tmp-dir   PATH  Directory per i file temporanei (default: /tmp)
//   --merge-fan N     Fan-in massimo del merge multi-pass (default: 16)
//   --multipass-merge Usa merge multi-pass semplice (default)
//   --legacy-merge    Alias storico di --multipass-merge
//   --flat-merge      Usa merge flat a due stadi
//   --pipeline-merge  Usa il merge in pipeline I/O asincrona
//   --keep-runs       Non eliminare i file di run dopo il merge (debug)
//
// ─────────────────────────────────────────────────────────────────────────────
// ARCHITETTURA
// ─────────────────────────────────────────────────────────────────────────────
//
// FASE 1  —  ff_sort_to_runs()   [ff_chunk_sorter.hpp]
//
//   ff::farm con Emitter + W Worker (no Collector):
//
//   ┌──────────┐      ┌──────────┐
//   │  Emitter │ ───▶ │ Worker 0 │ ──▶ run_0.bin
//   │ (legge   │ ───▶ │ Worker 1 │ ──▶ run_1.bin
//   │  chunk)  │  ... │   ...    │  ...
//   └──────────┘      └──────────┘
//
//   L'Emitter legge il file sequenzialmente e invia ogni chunk a un Worker.
//   FastFlow bilancia automaticamente il carico con la coda SPSC lock-free.
//   I Worker ordinano il chunk e scrivono la run su disco.
//
// FASE 2  —  selezione dell'algoritmo di merge [tre implementazioni]:
//   multipass (default) [ff_kway_merger.hpp]          — multi-pass semplice
//   flat                [ff_kway_merger.hpp]          — due stadi, stage 2 seriale
//   pipeline            [ff_kway_merger_pipeline.hpp] — I/O asincrona per confronto
//
// ─────────────────────────────────────────────────────────────────────────────
// DIFFERENZA RISPETTO ALLA VERSIONE OMP
// ─────────────────────────────────────────────────────────────────────────────
//   OMP Tasks:    overhead di creazione task ≈ qualche μs (barriera implicita)
//   FF Farm:      code lock-free SPSC; overhead di comunicazione ≈ ns
//                 Il thread Emitter non entra in regioni parallele:
//                 tutta la lettura è sul thread Emitter, zero contesa.
//   FF ParallelFor: nessun conflitto di CPU affinity tra Fase 1 e Fase 2;
//                 un solo runtime FF gestisce entrambe le fasi.
//
// =============================================================================

#include "ff_chunk_sorter.hpp"          // ffSortToRuns()
#include "ff_kway_merger.hpp"           // ffKwayMerge(), ffKwayMergeLegacy()
#include "ff_kway_merger_multipass_pipeline.hpp" // ffKwayMergeMultipassPipeline() — I/O asincrono + safe FD
#include "temp_dir.hpp"

#include <ff/ff.hpp>
#include <cstdio>
#include <stdexcept>
#include <iostream>
#include <string>
#include <chrono>
#include <cstdlib>

static double seconds(std::chrono::steady_clock::time_point a,
                      std::chrono::steady_clock::time_point b) {
    std::chrono::duration<double> elapsed = b - a;
    return elapsed.count();
}

static void usage(const char* prog) {
    std::cerr << "Utilizzo: " << prog << " <input> <output> [opzioni]\n"
              << "  --chunk-mb  N     Dimensione chunk in MB      (default: 256)\n"
              << "  --workers   N     Worker FastFlow             (default: max hw - 1)\n"
              << "  --tmp-dir   PATH  Directory file temporanei   (default: /tmp)\n"
              << "  --merge-fan N     Fan-in per merge multi-pass  (default: 16)\n"
              << "  --multipass-merge Usa merge multi-pass semplice (default)\n"
              << "  --legacy-merge    Alias storico di --multipass-merge\n"
              << "  --flat-merge      Usa merge flat a due stadi\n"
              << "  --pipeline-merge  Usa Multipass Pipeline con Writer asincrono\n"
              << "  --keep-runs       Non eliminare le run (debug)\n";
    std::exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        usage(argv[0]);
    }

    // Parametri di default.
    // Uso max core - 1 worker per lasciare un thread all'Emitter che legge.
    std::string inputPath  = argv[1];
    std::string outputPath = argv[2];
    std::string tmpDir     = "/tmp";
    size_t      chunkMb    = 256;
    int         mergeFan    = 16;
    bool        keepRuns    = false;
    bool        multipassMerge = true;
    bool        pipelineMerge = false;  // --pipeline-merge: I/O asincrona
    bool        mergeFanExplicit = false;

    // ff_numCores() restituisce il numero di core logici disponibili.
    // Riservo 1 core per l'Emitter, assegno gli altri ai Worker.
    int availableCores = static_cast<int>(ff_numCores());
    int nWorkers        = std::max(1, availableCores - 1);

    // Parsing volutamente speculare alla versione OpenMP.
    // Cosi' posso lanciare benchmark comparabili cambiando solo l'eseguibile.
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];

        if (a == "--chunk-mb" && i + 1 < argc) {
            chunkMb = std::stoul(argv[++i]);
        } else if (a == "--workers" && i + 1 < argc) {
            nWorkers = std::stoi(argv[++i]);
        } else if (a == "--tmp-dir" && i + 1 < argc) {
            tmpDir = argv[++i];
        } else if (a == "--merge-fan" && i + 1 < argc) {
            mergeFan = std::stoi(argv[++i]);
            mergeFanExplicit = true;
        } else if (a == "--legacy-merge" || a == "--multipass-merge") {
            multipassMerge = true;
            pipelineMerge = false;
        } else if (a == "--flat-merge") {
            multipassMerge = false;
            pipelineMerge = false;
        } else if (a == "--pipeline-merge") {
            pipelineMerge = true;
            multipassMerge = false;
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

    const size_t chunkBytes = chunkMb * 1024ULL * 1024ULL;

    if (pipelineMerge && !mergeFanExplicit) {
        mergeFan = FF_MULTIPASS_MERGE_FAN_DEFAULT;
    }

    // Directory temporanea unica per questa esecuzione FastFlow.
    TempDir workTmp(tmpDir, "spm_ff", keepRuns);

    std::cout << "=== FastFlow MergeSort out-of-core ===\n"
              << "  input        : " << inputPath     << "\n"
              << "  output       : " << outputPath    << "\n"
              << "  chunk        : " << chunkMb       << " MB\n"
              << "  workers      : " << nWorkers       << "\n"
              << "  merge impl   : " << (pipelineMerge ? "pipeline async I/O" :
                                          multipassMerge ? "simple multi-pass" :
                                                           "flat two-stage")   << "\n"
              << "  merge fan-in : " << (multipassMerge || pipelineMerge ? std::to_string(mergeFan) : "non usato") << "\n"
              << "  tmp          : " << workTmp.str() << "\n"
              << "  PAYLOAD_MAX  : " << PAYLOAD_MAX    << " B\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // FASE 1: sort parallelo dei chunk → file di run (FastFlow Farm)
    // ─────────────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();   // Inizio a contare il tempo.

    // ff_sort_to_runs implementa la fase 1 con una farm FastFlow.
    std::vector<std::string> runs =
        ffSortToRuns(inputPath, workTmp.str(), chunkBytes, nWorkers);   // Chiama la funzione ff_sort_to_runs per ordinare i chunk del file di input e salvare le run in file temporanei.

    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();   // Fine a contare il tempo.

    std::cout << "Fase 1 (sort FF): " << runs.size() << " run create in "
              << seconds(t0, t1) << " s\n";   // Stampa il tempo impiegato per la fase 1.

    if (runs.empty()) {
        FILE* empty = std::fopen(outputPath.c_str(), "wb");
        if (!empty) {
            throw std::runtime_error("ff_sort: impossibile creare output vuoto");
        }
        std::fclose(empty);
        std::cout << "File vuoto — output vuoto creato.\n";
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // FASE 2: K-way merge multi-pass → file di output (FastFlow ParallelFor)
    // ─────────────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();   // Inizio a contare il tempo.

    // ff_kway_merge usa ff::ParallelFor internamente: nessun conflitto di
    // CPU affinity con la farm usata nella Fase 1. Un solo runtime FF.
    bool deleteRuns = !keepRuns;
    if (pipelineMerge) {
        // Pipeline I/O asincrona: Reader a blocchi + Merger in RAM + Writer asincrono.
        // Usa un'architettura multi-pass per la sicurezza sui file descriptor.
        ffKwayMergeMultipassPipeline(runs, outputPath, nWorkers, mergeFan, deleteRuns);
    } else if (multipassMerge) {
        ffKwayMergeLegacy(runs, outputPath, nWorkers, mergeFan, deleteRuns);
    } else {
        ffKwayMerge(runs, outputPath, nWorkers, deleteRuns);
    }

    std::chrono::steady_clock::time_point t3 = std::chrono::steady_clock::now();   // Fine a contare il tempo.

    const double ts = seconds(t0, t1);  // Tempo impiegato per la fase 1.
    const double tm = seconds(t2, t3);  // Tempo impiegato per la fase 2.
    const double tt = seconds(t0, t3);  // Tempo totale impiegato.

    std::cout << "Fase 2 (merge): " << tm << " s\n\n"
              << "--- Riepilogo tempi ---\n"
              << "  Sort FF (Fase 1)    : " << ts << " s\n"
              << "  K-way merge (Fase 2): " << tm << " s\n"
              << "  Totale              : " << tt << " s\n";

    return 0;
}
