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
//   --merge-fan N     Fan-in massimo del K-way merge (default: 64)
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
// FASE 2  —  kway_merge()   [kway_merger.hpp]
//   Identica alla versione OMP: K-way merge multi-pass con fan-in limitato.
//   I gruppi indipendenti di una passata possono essere fusi in parallelo.
//
// ─────────────────────────────────────────────────────────────────────────────
// DIFFERENZA RISPETTO ALLA VERSIONE OMP
// ─────────────────────────────────────────────────────────────────────────────
//   OMP Tasks:    overhead di creazione task ≈ qualche μs (barriera implicita)
//   FF Farm:      code lock-free SPSC; overhead di comunicazione ≈ ns
//                 Il thread Emitter non entra in regioni parallele:
//                 tutta la lettura è sul thread Emitter, zero contesa.
//
// =============================================================================

#include "ff_chunk_sorter.hpp"  // ff_sort_to_runs()
#include "kway_merger.hpp"       // kway_merge()
#include "temp_dir.hpp"

#include <omp.h>
#include <ff/ff.hpp>
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
              << "  --merge-fan N     Fan-in massimo K-way merge  (default: 64)\n"
              << "  --keep-runs       Non eliminare le run (debug)\n";
    std::exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        usage(argv[0]);
    }

    // Parametri di default.
    // Uso max core - 1 worker per lasciare un thread all'Emitter che legge.
    std::string input_path  = argv[1];
    std::string output_path = argv[2];
    std::string tmp_dir     = "/tmp";
    size_t      chunk_mb    = 256;
    int         merge_fan   = 64;
    bool        keep_runs   = false;

    // ff_numCores() restituisce il numero di core logici disponibili.
    // Riservo 1 core per l'Emitter, assegno gli altri ai Worker.
    int available_cores = static_cast<int>(ff_numCores());
    int nworkers        = std::max(1, available_cores - 1);

    // Parsing volutamente speculare alla versione OpenMP.
    // Cosi' posso lanciare benchmark comparabili cambiando solo l'eseguibile.
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];

        if (a == "--chunk-mb" && i + 1 < argc) {
            chunk_mb = std::stoul(argv[++i]);
        } else if (a == "--workers" && i + 1 < argc) {
            nworkers = std::stoi(argv[++i]);
        } else if (a == "--tmp-dir" && i + 1 < argc) {
            tmp_dir = argv[++i];
        } else if (a == "--merge-fan" && i + 1 < argc) {
            merge_fan = std::stoi(argv[++i]);
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

    // omp_set_num_threads limita anche i thread usati durante il merge (Fase 2).
    omp_set_num_threads(nworkers);

    const size_t chunk_bytes = chunk_mb * 1024ULL * 1024ULL;

    // Directory temporanea unica per questa esecuzione FastFlow.
    TempDir work_tmp(tmp_dir, "spm_ff", keep_runs);

    std::cout << "=== FastFlow MergeSort out-of-core ===\n"
              << "  input        : " << input_path     << "\n"
              << "  output       : " << output_path    << "\n"
              << "  chunk        : " << chunk_mb       << " MB\n"
              << "  workers      : " << nworkers       << "\n"
              << "  merge fan-in : " << merge_fan      << "\n"
              << "  tmp          : " << work_tmp.str() << "\n"
              << "  PAYLOAD_MAX  : " << PAYLOAD_MAX    << " B\n\n";

    // ─────────────────────────────────────────────────────────────────────────
    // FASE 1: sort parallelo dei chunk → file di run (FastFlow Farm)
    // ─────────────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    // ff_sort_to_runs implementa la fase 1 con una farm FastFlow.
    std::vector<std::string> runs =
        ff_sort_to_runs(input_path, work_tmp.str(), chunk_bytes, nworkers);

    std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    std::cout << "Fase 1 (sort FF): " << runs.size() << " run create in "
              << seconds(t0, t1) << " s\n";

    if (runs.empty()) {
        std::cout << "File vuoto — output non creato.\n";
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // FASE 2: K-way merge multi-pass → file di output
    // Nota: la fase di merge è comune a entrambe le versioni (OMP e FF).
    // ─────────────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();

    // La fase di merge e' condivisa con OpenMP: tuttavia, per evitare che la
    // thread affinity impostata da FastFlow confini i thread OpenMP su un singolo core
    // (causando lock contention e crollo delle prestazioni), disabilitiamo il
    // parallelismo OpenMP in questa specifica chiamata.
    bool delete_runs   = !keep_runs;
    bool parallel_merge = false; // merge parallelo disabilitato per compatibilità con FF
    kway_merge(runs, output_path, merge_fan, delete_runs, parallel_merge);

    std::chrono::steady_clock::time_point t3 = std::chrono::steady_clock::now();

    const double ts = seconds(t0, t1);
    const double tm = seconds(t2, t3);
    const double tt = seconds(t0, t3);

    std::cout << "Fase 2 (merge): " << tm << " s\n\n"
              << "--- Riepilogo tempi ---\n"
              << "  Sort FF (Fase 1)    : " << ts << " s\n"
              << "  K-way merge (Fase 2): " << tm << " s\n"
              << "  Totale              : " << tt << " s\n";

    return 0;
}
