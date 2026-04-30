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
//   --merge-fan    N     Fan-in massimo del K-way merge (default: 64)
//   --no-par-merge       Disabilita merge parallelo tra gruppi (default: abilitato)
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
// FASE 2  —  kway_merge()   [kway_merger.hpp]
//   K-way merge multi-pass con fan-in limitato a --merge-fan:
//
//   Se le run sono ≤ merge-fan: un solo passaggio di merge.
//   Se le run sono > merge-fan: più passaggi a gruppi (ogni passata
//   riduce il numero di run di un fattore merge-fan) finché rimane
//   una sola run = file di output finale.
//
//   Questo garantisce che il numero di file aperti contemporaneamente
//   sia sempre ≤ merge-fan, controllando il consumo di RAM e file
//   descriptors anche con file di input enormi.
//
// ─────────────────────────────────────────────────────────────────────────────
// PARALLELISMO
// ─────────────────────────────────────────────────────────────────────────────
//   Fase 1: ogni chunk è ordinato in un task OMP → parallelismo CPU.
//   Fase 2: i gruppi di merge indipendenti all'interno di ogni passata
//           sono eseguiti come task OMP in parallelo (I/O-bound, utile
//           se il disco ha bandwidth sufficiente o se usiamo più spindle).
//
// =============================================================================

#include "chunk_sorter.hpp"  // sort_to_runs()
#include "kway_merger.hpp"   // kway_merge()
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
              << "  --merge-fan    N     Fan-in massimo K-way merge  (default: 64)\n"
              << "  --no-par-merge       Disabilita merge parallelo tra gruppi\n"
              << "  --keep-runs          Non eliminare le run (debug)\n";
    std::exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        usage(argv[0]);
    }

    // Parametri di default:
    // chunk grande abbastanza da sfruttare std::sort su blocchi corposi,
    // merge_fan alto per ridurre le passate su disco, ma ancora leggero in RAM.
    std::string input_path  = argv[1];
    std::string output_path = argv[2];
    std::string tmp_dir     = "/tmp";
    size_t      chunk_mb    = 256;
    int         nthreads    = omp_get_max_threads();
    int         merge_fan   = 64;
    bool        keep_runs   = false;
    bool        par_merge   = true;

    // Parsing semplice e diretto delle opzioni.
    // Mantengo nomi e parametri uguali tra versioni OMP, FF e MPI: rende piu'
    // facile confrontare i benchmark.
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];

        if (a == "--chunk-mb" && i + 1 < argc) {
            chunk_mb = std::stoul(argv[++i]);
        } else if (a == "--threads" && i + 1 < argc) {
            nthreads = std::stoi(argv[++i]);
        } else if (a == "--tmp-dir" && i + 1 < argc) {
            tmp_dir = argv[++i];
        } else if (a == "--merge-fan" && i + 1 < argc) {
            merge_fan = std::stoi(argv[++i]);
        } else if (a == "--no-par-merge") {
            par_merge = false;
        } else if (a == "--keep-runs") {
            keep_runs = true;
        } else {
            usage(argv[0]);
        }
    }

    if (chunk_mb == 0) {
        std::cerr << "[WARN] --chunk-mb 0 non valido, imposto a 1\n";
        chunk_mb = 1;
    }

    omp_set_num_threads(nthreads);

    // Converto chunk_mb in byte (1 MB = 1024 * 1024 byte).
    const size_t chunk_bytes = chunk_mb * 1024ULL * 1024ULL;

    // Ogni esecuzione usa una sottodirectory temporanea unica.
    // Se keep_runs=false viene cancellata automaticamente dal distruttore.
    TempDir work_tmp(tmp_dir, "spm_omp", keep_runs);

    std::cout << "=== OMP MergeSort out-of-core ===\n"
              << "  input        : " << input_path        << "\n"
              << "  output       : " << output_path       << "\n"
              << "  chunk        : " << chunk_mb          << " MB\n"
              << "  threads      : " << nthreads          << "\n"
              << "  merge fan-in : " << merge_fan         << "\n"
              << "  merge paral. : " << (par_merge ? "si" : "no") << "\n"
              << "  tmp          : " << work_tmp.str()    << "\n"
              << "  PAYLOAD_MAX  : " << PAYLOAD_MAX       << " B\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // FASE 1: sort parallelo dei chunk → file di run
    // ─────────────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    // sort_to_runs e' il cuore della fase 1:
    // produce tanti file temporanei ordinati, uno per chunk.
    std::vector<std::string> runs = sort_to_runs(input_path, work_tmp.str(), chunk_bytes);

    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    std::cout << "Fase 1 (sort): " << runs.size() << " run create in "
              << seconds(t0, t1) << " s\n";

    if (runs.empty()) {
        std::cout << "File vuoto — output non creato.\n";
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // FASE 2: K-way merge multi-pass → file di output
    // ─────────────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();

    // Il merge lavora solo su file ordinati: non carica mai tutte le run in RAM.
    kway_merge(runs, output_path, merge_fan, /*delete_runs=*/!keep_runs,
               /*parallel_merge=*/par_merge);

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
