// =============================================================================
// mpi_sort.cpp - MergeSort out-of-core distribuito (MPI + OpenMP)
// =============================================================================
//
// Utilizzo:
//   mpirun -n P ./mpi_sort <input> <output> [opzioni]
//
//   --chunk-mb  N     Dimensione del blocco in RAM per ogni run locale (default: 256 MB)
//   --threads   N     Numero di thread OpenMP per rank (default: max hw)
//   --tmp-dir   PATH  Directory per i file temporanei (default: /tmp)
//   --merge-fan N     Fan-in massimo del K-way merge locale (default: 64)
//
// ARCHITETTURA DISTRIBUITA
//
// L'algoritmo sfrutta P nodi/rank MPI per ordinare un file > RAM in due fasi:
//
// FASE 1: Sort Locale (I/O parallelo e OpenMP tasking)
//   - Rank 0 fa una passata sugli header e calcola P+1 offset che cadono
//     esattamente su boundary di record. Poi li distribuisce con MPI_Bcast.
//   - Ogni rank legge la propria stripe, applica sort_range_to_runs (usando task
//     OpenMP internamente) e fa un K-way merge locale, producendo un singolo
//     file `local_sorted.bin`.
//
// FASE 2: Binary Tree Merge Distribuito
//   - I P file locali vengono fusi usando un albero binario.
//   - Allo Step S (S=1, 2, 4, 8...):
//       * I rank mittenti inviano (con blocchi MPI_Send) il loro file corrente
//         ai rank riceventi.
//       * I rank riceventi salvano il file su disco locale.
//       * Il rank ricevente esegue un 2-way merge out-of-core tra il proprio
//         file e quello appena ricevuto.
//   - Alla fine log_2(P) step, il rank 0 avrà il file completamente ordinato,
//     e lo rinomina in <output>.
//
// Perche' calcolo prima i boundary?
// Il formato dei record non contiene un magic number. Quindi non e' sicuro
// prendere un offset casuale nel file e cercare "un header plausibile":
// byte del payload potrebbero sembrare un header valido. Per questo rank 0
// scorre il file record per record e sceglie solo offset sicuramente corretti.
//
// Perche' merge ad albero?
// Se tutti i rank inviassero direttamente a rank 0, il master diventerebbe
// subito il collo di bottiglia. Con l'albero, il lavoro di merge e parte della
// comunicazione sono distribuiti su piu' rank.
// =============================================================================

#include "chunk_sorter.hpp"
#include "kway_merger.hpp"
#include "temp_dir.hpp"

#include <mpi.h>
#include <omp.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <stdexcept>
#include <sys/stat.h>

// MPI_Wtime e' il timer standard MPI: comodo per misurare anche run multi-rank.
static double wall() {
    return MPI_Wtime();
}

static void usage(const char* prog) {
    std::cerr << "Utilizzo: mpirun -n P " << prog << " <input> <output> [opzioni]\n"
              << "  --chunk-mb  N     MB per chunk locale (default: 256)\n"
              << "  --threads   N     Thread OpenMP per rank (default: max hw)\n"
              << "  --tmp-dir   PATH  Directory temporanea (default: /tmp)\n"
              << "  --merge-fan N     Fan-in K-way merge locale (default: 64)\n";
    std::exit(1);
}

// Dimensione del file in byte.
// Uso int64_t per evitare problemi con file grandi (> 2 GB).
static int64_t file_size(const std::string& path) {
    struct stat st{};
    int ret = ::stat(path.c_str(), &st);
    if (ret != 0) {
        return -1;
    }
    return static_cast<int64_t>(st.st_size);
}

// compute_record_boundaries
//
// Rank 0 calcola P+1 offset:
//
//   boundaries[0]      = 0
//   boundaries[rank]   = inizio della stripe del rank
//   boundaries[P]      = fine file
//
// Ogni boundary deve cadere esattamente all'inizio di un record.
//
// Il target teorico e':
//
//   file_size * rank / nprocs
//
// ma se quel target cade dentro un payload, scelgo il primo inizio-record dopo
// il target. Il bilanciamento non e' perfetto al byte, ma e' corretto e semplice.
static std::vector<int64_t> compute_record_boundaries(
    const std::string& path,
    int64_t            file_sz,
    int                nprocs)
{
    // Inizializzo tutti i boundary a file_sz (valore "fine file").
    // I boundary che riesco a calcolare verranno sovrascritti.
    std::vector<int64_t> boundaries(nprocs + 1, file_sz);
    boundaries[0] = 0;

    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        throw std::runtime_error("compute_record_boundaries: impossibile aprire " + path);
    }
    std::setvbuf(f, nullptr, _IOFBF, 4 * 1024 * 1024);

    int next_rank = 1;
    while (next_rank < nprocs) {
        // pos e' sempre un inizio-record, perche' avanzo solo con read_header
        // + skip_payload.
        off_t pos = ftello(f);
        if (pos < 0 || pos >= static_cast<off_t>(file_sz)) {
            break;
        }

        // Target teorico: punto del file che dovrebbe essere l'inizio della stripe
        // del rank next_rank (divisione uniforme del file in nprocs parti).
        int64_t target = (file_sz * static_cast<int64_t>(next_rank)) / nprocs;

        if (static_cast<int64_t>(pos) >= target) {
            // Ho superato il target del rank: questo e' un boundary valido.
            // La posizione corrente e' garantita essere un inizio-record.
            boundaries[next_rank] = static_cast<int64_t>(pos);
            ++next_rank;
            continue;
        }

        // Non ho ancora raggiunto il target: avanzo al record successivo.
        RecordHeader hdr;
        bool got_record = read_header(f, hdr);
        if (!got_record) {
            break; // EOF inatteso
        }
        skip_payload(f, hdr.len);
    }

    // I rank rimanenti (se il file ha meno record di nprocs) ricevono
    // una stripe vuota: [file_sz, file_sz).
    while (next_rank < nprocs) {
        boundaries[next_rank] = file_sz;
        ++next_rank;
    }
    boundaries[nprocs] = file_sz;

    std::fclose(f);
    return boundaries;
}

// Trasferimento file via MPI.
//
// Durante il merge ad albero un rank mittente invia il file ordinato locale al
// rank ricevente. Non mando il file in un unico messaggio enorme: lo divido in
// blocchi da 256 MB, abbastanza grandi da ridurre overhead ma ancora gestibili.
static constexpr size_t MPI_CHUNK = 256ULL * 1024 * 1024;

static void mpi_send_file(const std::string& path, int dest,
                          int tag_size, int tag_data, MPI_Comm comm)
{
    // Prima mando la dimensione, cosi' il ricevente sa quanti byte aspettarsi.
    int64_t sz = file_size(path);
    MPI_Send(&sz, 1, MPI_INT64_T, dest, tag_size, comm);

    // File vuoto o inesistente: nulla da inviare.
    if (sz <= 0) {
        return;
    }

    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        throw std::runtime_error("mpi_send_file: impossibile aprire " + path);
    }

    std::vector<char> buf(MPI_CHUNK);
    int64_t remaining = sz;

    while (remaining > 0) {
        // Calcolo la dimensione del blocco corrente: l'ultimo e' eventualmente piu' piccolo.
        int64_t batch_64 = std::min(static_cast<int64_t>(MPI_CHUNK), remaining);
        size_t  batch    = static_cast<size_t>(batch_64);

        size_t bytes_read = std::fread(buf.data(), 1, batch, f);
        if (bytes_read != batch) {
            throw std::runtime_error("mpi_send_file: lettura troncata");
        }

        // MPI_Send vuole un int per il count: sicuro perché batch <= 256 MB < INT_MAX.
        MPI_Send(buf.data(), static_cast<int>(batch), MPI_BYTE, dest, tag_data, comm);
        remaining -= batch_64;
    }

    std::fclose(f);
}

static void mpi_recv_file(const std::string& path, int src,
                          int tag_size, int tag_data, MPI_Comm comm)
{
    // Ricevo prima la dimensione inviata da mpi_send_file.
    int64_t sz;
    MPI_Recv(&sz, 1, MPI_INT64_T, src, tag_size, comm, MPI_STATUS_IGNORE);

    // File vuoto: nulla da ricevere.
    if (sz <= 0) {
        return;
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        throw std::runtime_error("mpi_recv_file: impossibile creare " + path);
    }
    std::setvbuf(f, nullptr, _IOFBF, 8 * 1024 * 1024);

    std::vector<char> buf(MPI_CHUNK);
    int64_t remaining = sz;

    while (remaining > 0) {
        int64_t batch_64 = std::min(static_cast<int64_t>(MPI_CHUNK), remaining);
        size_t  batch    = static_cast<size_t>(batch_64);

        MPI_Recv(buf.data(), static_cast<int>(batch), MPI_BYTE, src, tag_data, comm,
                 MPI_STATUS_IGNORE);

        size_t bytes_written = std::fwrite(buf.data(), 1, batch, f);
        if (bytes_written != batch) {
            throw std::runtime_error("mpi_recv_file: scrittura fallita");
        }

        remaining -= batch_64;
    }

    std::fclose(f);
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    int provided;
    // MPI_THREAD_FUNNELED significa: il processo puo' usare thread OpenMP, ma
    // solo il thread principale fa chiamate MPI. E' sufficiente per questo
    // programma, perche' le MPI_Send/MPI_Recv non sono dentro regioni parallele.
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        std::cerr << "[WARN] MPI non supporta MPI_THREAD_FUNNELED, continuo con "
                  << provided << "\n";
    }

    int rank;
    int nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    if (argc < 3) {
        if (rank == 0) {
            usage(argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    std::string input_path  = argv[1];
    std::string output_path = argv[2];
    std::string tmp_dir     = "/tmp";
    size_t      chunk_mb    = 256;
    int         nthreads    = omp_get_max_threads();
    int         merge_fan   = 64;

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
        } else {
            if (rank == 0) {
                usage(argv[0]);
            }
            MPI_Finalize();
            return 1;
        }
    }

    if (chunk_mb == 0) {
        if (rank == 0) {
            std::cerr << "[WARN] --chunk-mb 0 non valido, imposto a 1\n";
        }
        chunk_mb = 1;
    }

    omp_set_num_threads(nthreads);
    const size_t chunk_bytes = chunk_mb * 1024ULL * 1024ULL;

    // Directory temporanea unica per rank: evita collisioni tra run concorrenti.
    std::string rank_prefix = "spm_mpi_r" + std::to_string(rank);
    TempDir work_tmp(tmp_dir, rank_prefix);
    std::string my_tmp = work_tmp.str();

    if (rank == 0) {
        std::cout << "=== MPI+OMP MergeSort out-of-core ===\n"
                  << "  ranks    : " << nprocs   << "\n"
                  << "  threads  : " << nthreads << "\n"
                  << "  chunk    : " << chunk_mb << " MB\n"
                  << "  fan-in   : " << merge_fan << "\n"
                  << "  tmp base : " << tmp_dir  << "\n\n";
    }
    MPI_Barrier(MPI_COMM_WORLD);

    double t_start = wall();
    double t1a     = wall();

    // -------------------------------------------------------------------------
    // FASE 1: Sort Locale (Stripe)
    // -------------------------------------------------------------------------
    int64_t total_bytes = file_size(input_path);
    if (total_bytes < 0) {
        std::cerr << "[rank " << rank << "] impossibile aprire " << input_path << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Vettore di P+1 offset: boundaries[r] = inizio stripe del rank r.
    std::vector<int64_t> boundaries(nprocs + 1, 0);

    // Rank 0 calcola i boundary corretti e li distribuisce a tutti.
    if (rank == 0) {
        boundaries = compute_record_boundaries(input_path, total_bytes, nprocs);
    }
    // MPI_Bcast: rank 0 invia il vettore a tutti gli altri rank.
    MPI_Bcast(boundaries.data(), nprocs + 1, MPI_INT64_T, 0, MPI_COMM_WORLD);

    // Ogni rank lavora solo sul proprio intervallo [my_start, my_end).
    int64_t my_start = boundaries[rank];
    int64_t my_end   = boundaries[rank + 1];

    std::string local_sorted = my_tmp + "/local_sorted.bin";
    {
        // sort_range_to_runs: legge solo il range [my_start, my_end) del file.
        std::vector<std::string> run_paths =
            sort_range_to_runs(input_path, my_tmp, chunk_bytes, my_start, my_end);

        if (run_paths.empty()) {
            // Stripe vuota: creo comunque il file (vuoto) per uniformità.
            FILE* f = std::fopen(local_sorted.c_str(), "wb");
            if (f != nullptr) {
                std::fclose(f);
            }
        } else if (run_paths.size() == 1) {
            // Una sola run: rinomino direttamente senza merge.
            if (std::rename(run_paths[0].c_str(), local_sorted.c_str()) != 0) {
                throw std::runtime_error("mpi_sort: rename fallito");
            }
        } else {
            // Più run locali: le fondo in un unico file locale.
            // Merge locale seriale (parallel_merge=false) per evitare
            // di saturare il disco mentre altri rank fanno lo stesso.
            kway_merge(run_paths, local_sorted, merge_fan,
                       /*delete_runs=*/true, /*parallel_merge=*/false);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t1b = wall();
    if (rank == 0) {
        std::cout << "Fase 1 (sort locale): " << (t1b - t1a) << " s\n";
    }

    // -------------------------------------------------------------------------
    // FASE 2: Tree Merge Distribuito
    // -------------------------------------------------------------------------
    // Schema di riduzione ad albero (esempio con 4 rank):
    //
    // step=1:  rank 1 → rank 0    rank 3 → rank 2
    // step=2:  rank 2 → rank 0
    //
    // Dopo ogni ricezione il rank ricevente fonde il proprio file corrente con
    // quello ricevuto. Alla fine rank 0 contiene tutto.
    double t2a = wall();
    std::string current_file = local_sorted;

    for (int step = 1; step < nprocs; step *= 2) {
        // group_size e' la dimensione del "gruppo" a questo passo dell'albero.
        int group_size = step * 2;

        // my_group e' il rank del primo elemento del gruppo a cui appartengo.
        int my_group = (rank / group_size) * group_size;

        // is_receiver: sono il nodo 0 del mio gruppo (accumulo il risultato).
        bool is_receiver = (rank % group_size == 0);

        // is_sender: sono il nodo 'step' del mio gruppo (invio il mio file).
        bool is_sender = (rank % group_size == step);

        // Gli altri rank non partecipano a questo passo.
        if (!is_receiver && !is_sender) {
            continue;
        }

        if (is_sender) {
            // Il mittente invia il file corrente al suo receiver e poi termina.
            int receiver = my_group;
            int tag_size = 100 + step;
            int tag_data = 200 + step;
            mpi_send_file(current_file, receiver, tag_size, tag_data, MPI_COMM_WORLD);

            // Rimuovo il file locale: non mi serve più.
            if (std::remove(current_file.c_str()) != 0) {
                std::fprintf(stderr, "[WARN] mpi_sort: impossibile rimuovere %s\n",
                             current_file.c_str());
            }
            current_file = "";

        } else { // is_receiver
            // Il ricevente controlla se il partner esiste: cosi' funziona anche
            // con un numero di rank non potenza di due.
            int sender = rank + step;
            if (sender < nprocs) {
                int tag_size = 100 + step;
                int tag_data = 200 + step;

                // Salvo il file ricevuto con un nome univoco per questo step.
                std::string recv_path  = my_tmp + "/recv_step" + std::to_string(step) + ".bin";
                mpi_recv_file(recv_path, sender, tag_size, tag_data, MPI_COMM_WORLD);

                // Merge out-of-core a 2 vie tra il file locale e quello ricevuto.
                std::string merged_path = my_tmp + "/merged_step" + std::to_string(step) + ".bin";
                std::vector<std::string> pair_runs = {current_file, recv_path};
                kway_merge(pair_runs, merged_path, /*merge_fan=*/2,
                           /*delete_runs=*/true, /*parallel_merge=*/false);

                // Il file fuso diventa il nuovo "file corrente" per i passi successivi.
                current_file = merged_path;
            }
        }
    }

    double t2b = wall();
    if (rank == 0) {
        std::cout << "Fase 2 (merge distribuito): " << (t2b - t2a) << " s\n";
    }

    // Rank 0 rinomina il file corrente come output finale.
    if (rank == 0) {
        if (current_file != output_path) {
            if (std::rename(current_file.c_str(), output_path.c_str()) != 0) {
                throw std::runtime_error("mpi_sort: rename finale fallito");
            }
        }
        double t_end = wall();
        std::cout << "\n--- Riepilogo tempi (rank 0) ---\n"
                  << "  Sort locale  (Fase 1) : " << (t1b - t1a)       << " s\n"
                  << "  Merge dist.  (Fase 2) : " << (t2b - t2a)       << " s\n"
                  << "  Totale               : " << (t_end - t_start)  << " s\n";
    }

    MPI_Finalize();
    return 0;
}
