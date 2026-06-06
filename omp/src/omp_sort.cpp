/*
Modulo di main che chiama il chunk_sorter e il kway_merger della versione OpenMP. 
Si prendono da argv la dimensione del chunk in MB, il numero di thread OpenMP, il path della directory dove mettere i file temporanei, il merge fan massimo K per il merge, e i flag per usare il parallelismo per i gruppi di run nello stesso livello di merge e per mantenere le run. Se non presenti su argv, vengono inizializzati a dei valori di default.
Si esegue sort_to_runs(), che a partire dal file di input produce per ogni chunk una run ordinata.
Si chiama ompKwayMerge(), che esegue il merge kway multilivello, partendo da un insieme di run ordinate, e ritornando un unico file di output ordinato. 
*/
// Utilizzo:
//   ./omp_sort <input> <output> [opzioni]
//
//   --chunk-mb     N     Dimensione del blocco in RAM per ogni run (default: 256 MB)
//   --threads      N     Numero di thread OpenMP (default: max hw)
//   --tmp-dir      PATH  Directory per i file temporanei (default: /scratch)
//   --merge-fan    N     Fan-in massimo del merge multi-pass (default: 64)
//   --multipass-merge    Usa merge multi-pass semplice (default)
//   --no-par-merge       Disabilita merge parallelo
//   --keep-runs          Non eliminare i file di run dopo il merge (debug)
//


#include "chunk_sorter.hpp"               // sort_to_runs()
#include "omp_kway_merger.hpp"            // ompKwayMergeMultipass()
#include "temp_dir.hpp"

#include <iostream>
#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
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
              << "  --tmp-dir      PATH  Directory file temporanei   (default: /scratch)\n"
              << "  --merge-fan    N     Fan-in per merge multi-pass  (default: 64)\n"
              << "  --multipass-merge    Usa merge multi-pass semplice (default)\n"
              << "  --no-par-merge       Disabilita merge parallelo\n"
              << "  --keep-runs          Non eliminare le run (debug)\n";
    std::exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        usage(argv[0]);
    }

    // Parametri di default
    std::string inputPath  = argv[1];
    std::string outputPath = argv[2];
    std::string tmpDir     = "/scratch";
    size_t      chunkMb    = 64;
    int         nThreads    = omp_get_max_threads();
    int         mergeFan   = 8;
    bool        keepRuns        = false;
    bool        parallelMerge   = true;

    // Parsing semplice e diretto delle opzioni.
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
        } else if (a == "--multipass-merge") {
            // Default esplicito, accettato per compatibilita' con gli script.
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
    std::cout << "=== OMP MergeSort out-of-core ===\n"
              << "  input        : " << inputPath        << "\n"
              << "  output       : " << outputPath       << "\n"
              << "  chunk        : " << chunkMb          << " MB\n"
              << "  threads      : " << nThreads          << "\n"
              << "  merge impl   : simple multi-pass\n"
              << "  merge fan-in : " << mergeFan << "\n"
              << "  merge paral. : " << (parallelMerge ? "si" : "no") << "\n"
              << "  tmp          : " << workTmp.str()    << "\n"
              << "  PAYLOAD_MAX  : " << PAYLOAD_MAX       << " B\n\n";


    // FASE 1: sort parallelo dei chunk → file di run
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    std::vector<std::string> runs = sortToRuns(inputPath, workTmp.str(), chunkBytes);
    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    std::cout << "Fase 1 (sort): " << runs.size() << " run create in "
              << seconds(t0, t1) << " s\n";

    if (runs.empty()) {
        FILE* empty = std::fopen(outputPath.c_str(), "wb");
        if (!empty) {
            throw std::runtime_error("omp_sort: impossibile creare output vuoto");
        }
        std::fclose(empty);
        std::cout << "File vuoto — output vuoto creato.\n";
        return 0;
    }

    // FASE 2: K-way merge → file di output
    std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
    ompKwayMergeMultipass(runs, outputPath, mergeFan, /*deleteRuns=*/!keepRuns,
                          /*parallelMerge=*/parallelMerge);
    std::chrono::steady_clock::time_point t3 = std::chrono::steady_clock::now();


    // Report finale
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
