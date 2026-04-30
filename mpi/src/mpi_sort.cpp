// =============================================================================
// mpi_sort.cpp  –  MergeSort out-of-core distribuito (MPI + OpenMP)
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
// =============================================================================
// ARCHITETTURA DISTRIBUITA
// =============================================================================
//
// FASE 1 – Sort locale (parallelo su tutti i rank)
// ─────────────────────────────────────────────────
//   1. Rank 0 scorre gli header del file e calcola P+1 offset che cadono
//      esattamente all'inizio di un record (boundary sicuri).
//      Poi li distribuisce a tutti con MPI_Bcast.
//
//   2. Ogni rank legge solo la propria stripe [myStart, myEnd), la divide
//      in chunk da chunkMb MB, ordina ogni chunk con OpenMP task, e infine
//      fonde i chunk con un K-way merge locale → local_sorted.bin.
//
// FASE 2 – Binary tree merge distribuito
// ────────────────────────────────────────
//   L'albero di riduzione funziona così (esempio con P=4):
//
//     step=1:  rank 1 → rank 0      rank 3 → rank 2
//     step=2:  rank 2 → rank 0
//
//   Ad ogni step:
//     * Il rank "mittente"  invia il file locale al rank "ricevente".
//     * Il rank "ricevente" esegue un merge out-of-core 2-way tra il suo
//       file corrente e quello ricevuto.
//
//   Ottimizzazione chiave – Pipelining doppio buffer:
//     L'invio avviene a blocchi (PIPE_CHUNK byte).  Mentre il blocco N è
//     in transito via MPI_Isend/MPI_Wait, il mittente legge già il blocco
//     N+1 dal disco.  Il ricevente, simmetricamente, mentre MPI_Irecv
//     aspetta il blocco N+1, scrive su disco il blocco N.
//     In questo modo latenza di rete e I/O disco si sovrappongono.
//
// Perché i boundary calcolati da rank 0?
//   Il formato dei record non ha magic number: un byte del payload potrebbe
//   sembrare un header valido.  Rank 0 scorre il file record per record e
//   garantisce che ogni boundary sia un vero inizio-record.
//
// Perché merge ad albero e non tutti → rank 0?
//   Se tutti i rank inviassero a rank 0, il master sarebbe il collo di
//   bottiglia sia in rete sia in I/O.  Con l'albero, merge e comunicazione
//   sono distribuiti su più rank.
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
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

// MPI_Wtime è il timer ad alta risoluzione fornito da MPI: funziona
// su tutti i rank e garantisce una base di tempo coerente tra nodi.
static double wall() { return MPI_Wtime(); }

static void usage(const char* prog) {
    std::cerr << "Utilizzo: mpirun -n P " << prog << " <input> <output> [opzioni]\n"
              << "  --chunk-mb  N     MB per chunk locale (default: 256)\n"
              << "  --threads   N     Thread OpenMP per rank (default: max hw)\n"
              << "  --tmp-dir   PATH  Directory temporanea (default: /tmp)\n"
              << "  --merge-fan N     Fan-in K-way merge locale (default: 64)\n";
    std::exit(1);
}

// ─── Dimensione del file ──────────────────────────────────────────────────────
// int64_t è necessario: size_t è unsigned e su alcuni sistemi è 32 bit,
// il che causerebbe overflow con file > 4 GB.
static int64_t fileSize(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return -1;
    return static_cast<int64_t>(st.st_size);
}

// =============================================================================
// computeRecordBoundaries
// =============================================================================
// Calcola P+1 offset nel file che cadono esattamente all'inizio di un record.
//
//   boundaries[0]   = 0           (sempre)
//   boundaries[r]   = byte di inizio della stripe del rank r
//   boundaries[P]   = fileSize    (sempre)
//
// Algoritmo:
//   Si scorre il file record per record.  Ogni volta che la posizione
//   corrente supera il "target" teorico (fileSize * r / P), quella
//   posizione diventa boundaries[r].
//   Così il boundary è sempre un inizio-record garantito.
//
// Complessità: O(N) letture di header (solo rank 0, una sola volta).
static std::vector<int64_t> computeRecordBoundaries(
    const std::string& path,
    int64_t            totalSize,
    int                numProcs)
{
    // Inizializzo tutti i boundary a totalSize ("fine file").
    // I rank con stripe vuota manterranno questo valore → stripe [totalSize, totalSize).
    std::vector<int64_t> boundaries(numProcs + 1, totalSize);
    boundaries[0]        = 0;
    boundaries[numProcs] = totalSize;

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("computeRecordBoundaries: impossibile aprire " + path);

    try {
        // Buffer grande per ridurre le syscall durante la scansione sequenziale.
        std::setvbuf(f, nullptr, _IOFBF, 4 * 1024 * 1024);

        int nextRank = 1;
        while (nextRank < numProcs) {
            // pos è sempre un inizio-record: avanziamo solo con readHeader + skipPayload.
            off_t pos = ftello(f);
            if (pos < 0 || pos >= static_cast<off_t>(totalSize)) break;

            // Target teorico per il rank nextRank: divisione uniforme in byte.
            int64_t target = (totalSize * static_cast<int64_t>(nextRank)) / numProcs;

            if (static_cast<int64_t>(pos) >= target) {
                // Abbiamo raggiunto o superato il target: questa è un'inizio-record valido.
                boundaries[nextRank] = static_cast<int64_t>(pos);
                ++nextRank;
                continue; // Non consumiamo il record: potrebbe servire per il boundary successivo.
            }

            // Non ancora al target: saltiamo il record corrente.
            RecordHeader hdr;
            if (!readHeader(f, hdr)) break; // EOF inatteso
            skipPayload(f, hdr.len);
        }

        std::fclose(f);
    } catch (...) {
        std::fclose(f);
        throw;
    }
    return boundaries;
}

// =============================================================================
// Pipelining doppio buffer – invio
// =============================================================================
// Blocco di trasferimento: 64 MB bilancia overhead MPI e larghezza di banda.
// Blocchi troppo piccoli → overhead per messaggio elevato.
// Blocchi troppo grandi → meno overlap tra disco e rete.
static constexpr size_t PIPE_CHUNK = 64ULL * 1024 * 1024;

// mpiSendFile
// ─────────────
// Invia il file `path` al rank `dest` usando un pattern doppio buffer:
//
//   [leggi blocco 0 dal disco]
//   loop:
//     [invia blocco i via MPI_Isend]  ← non bloccante
//     [leggi blocco i+1 dal disco]    ← avviene in parallelo all'invio
//     MPI_Wait(invio precedente)      ← aspetta solo ora
//
// In questo modo la latenza di rete e il tempo di lettura disco si
// sovrappongono, aumentando l'utilizzo effettivo della banda di rete.
static void mpiSendFile(const std::string& path, int dest,
                        int tagSize, int tagData, MPI_Comm comm)
{
    // Prima cosa: comunico la dimensione totale al ricevente, così sa
    // quanti byte aspettarsi e può terminare il loop di ricezione.
    int64_t sz = fileSize(path);
    MPI_Send(&sz, 1, MPI_INT64_T, dest, tagSize, comm);

    if (sz <= 0) return; // File vuoto: nulla da inviare.

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("mpiSendFile: impossibile aprire " + path);

    // Due buffer alternati: mentre uno è "in volo" (MPI_Isend), l'altro
    // viene riempito dal disco. Si evita così di copiare tra buffer e di
    // bloccarsi in attesa della rete prima di leggere il prossimo blocco.
    std::vector<char> bufA(PIPE_CHUNK);
    std::vector<char> bufB(PIPE_CHUNK);
    char* sendBuf = bufA.data(); // buffer attualmente in invio
    char* readBuf = bufB.data(); // buffer attualmente in lettura

    int64_t remaining = sz;
    MPI_Request req = MPI_REQUEST_NULL;

    // Pre-leggo il primo blocco prima di entrare nel loop.
    size_t firstBatch = static_cast<size_t>(std::min(static_cast<int64_t>(PIPE_CHUNK), remaining));
    if (std::fread(sendBuf, 1, firstBatch, f) != firstBatch)
        throw std::runtime_error("mpiSendFile: lettura troncata (primo blocco)");
    remaining -= static_cast<int64_t>(firstBatch);

    size_t pendingSize = firstBatch; // dimensione del blocco che stiamo per inviare

    while (pendingSize > 0) {
        // Lancio l'invio non bloccante del blocco corrente.
        MPI_Isend(sendBuf, static_cast<int>(pendingSize), MPI_BYTE, dest, tagData, comm, &req);

        size_t nextBatch = 0;
        if (remaining > 0) {
            // Mentre la rete trasporta il blocco corrente, leggo il prossimo dal disco.
            nextBatch = static_cast<size_t>(std::min(static_cast<int64_t>(PIPE_CHUNK), remaining));
            if (std::fread(readBuf, 1, nextBatch, f) != nextBatch)
                throw std::runtime_error("mpiSendFile: lettura troncata");
            remaining -= static_cast<int64_t>(nextBatch);
        }

        // Solo ora aspetto che l'invio precedente sia completato.
        MPI_Wait(&req, MPI_STATUS_IGNORE);

        // Scambio i due buffer: il buffer appena spedito diventa quello di lettura,
        // e il buffer appena riempito diventa quello da spedire.
        std::swap(sendBuf, readBuf);
        pendingSize = nextBatch;
    }

    std::fclose(f);
}

// =============================================================================
// Pipelining doppio buffer – ricezione
// =============================================================================
// mpiRecvFile
// ────────────
// Riceve il file dal rank `src` con lo stesso schema doppio buffer:
//
//   [ricevi blocco 0 via MPI_Irecv]  ← non bloccante
//   loop:
//     MPI_Wait(ricezione i)          ← aspetta blocco i
//     [scrivi blocco i su disco]     ← scrittura del blocco ricevuto
//     [lancia MPI_Irecv blocco i+1] ← in parallelo alla scrittura
//
// In questo modo la scrittura su disco del blocco precedente si sovrappone
// alla ricezione via rete del blocco successivo.
static void mpiRecvFile(const std::string& path, int src,
                        int tagSize, int tagData, MPI_Comm comm)
{
    // Ricevo prima la dimensione totale comunicata dal mittente.
    int64_t sz;
    MPI_Recv(&sz, 1, MPI_INT64_T, src, tagSize, comm, MPI_STATUS_IGNORE);

    if (sz <= 0) return; // File vuoto: nulla da ricevere.

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("mpiRecvFile: impossibile creare " + path);

    // Buffer grande per ammortizzare le syscall di scrittura.
    std::setvbuf(f, nullptr, _IOFBF, 8 * 1024 * 1024);

    // Due buffer alternati, stesso schema del sender.
    std::vector<char> bufA(PIPE_CHUNK);
    std::vector<char> bufB(PIPE_CHUNK);
    char* recvBuf  = bufA.data(); // buffer in cui la rete sta scrivendo
    char* writeBuf = bufB.data(); // buffer che stiamo scrivendo su disco

    int64_t remaining = sz;
    MPI_Request req = MPI_REQUEST_NULL;

    // Pre-lancio la ricezione del primo blocco.
    size_t firstBatch = static_cast<size_t>(std::min(static_cast<int64_t>(PIPE_CHUNK), remaining));
    MPI_Irecv(recvBuf, static_cast<int>(firstBatch), MPI_BYTE, src, tagData, comm, &req);
    remaining -= static_cast<int64_t>(firstBatch);

    size_t pendingSize = firstBatch;

    while (pendingSize > 0) {
        // Aspetto che la ricezione corrente sia terminata.
        MPI_Wait(&req, MPI_STATUS_IGNORE);
        // Ora recvBuf contiene dati validi: lo scambio con writeBuf.
        std::swap(recvBuf, writeBuf);

        size_t nextBatch = 0;
        if (remaining > 0) {
            // Lancio la ricezione del blocco successivo prima di scrivere su disco:
            // rete e disco lavorano in parallelo.
            nextBatch = static_cast<size_t>(std::min(static_cast<int64_t>(PIPE_CHUNK), remaining));
            MPI_Irecv(recvBuf, static_cast<int>(nextBatch), MPI_BYTE, src, tagData, comm, &req);
            remaining -= static_cast<int64_t>(nextBatch);
        }

        // Scrivo il blocco appena ricevuto su disco.
        if (std::fwrite(writeBuf, 1, pendingSize, f) != pendingSize)
            throw std::runtime_error("mpiRecvFile: scrittura fallita");

        pendingSize = nextBatch;
    }

    std::fclose(f);
}

// =============================================================================
// MAIN
// =============================================================================
int main(int argc, char* argv[]) {
    int provided;
    // MPI_THREAD_FUNNELED: il processo usa thread OpenMP, ma solo il thread
    // principale esegue chiamate MPI.  È sufficiente perché le MPI_Send/Recv
    // sono sempre fuori dalle regioni parallele OpenMP.
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        std::cerr << "[WARN] MPI non supporta MPI_THREAD_FUNNELED, continuo con "
                  << provided << "\n";
    }

    int rank, numProcs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs);

    // ── Parsing argomenti ──────────────────────────────────────────────────────
    if (argc < 3) {
        if (rank == 0) usage(argv[0]);
        MPI_Finalize();
        return 1;
    }

    std::string inputPath  = argv[1];
    std::string outputPath = argv[2];
    std::string tmpDir     = "/tmp";
    size_t      chunkMb    = 256;
    int         nThreads   = omp_get_max_threads();
    int         mergeFan   = 64;

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--chunk-mb"  && i + 1 < argc) chunkMb  = std::stoul(argv[++i]);
        else if (a == "--threads"   && i + 1 < argc) nThreads = std::stoi(argv[++i]);
        else if (a == "--tmp-dir"   && i + 1 < argc) tmpDir   = argv[++i];
        else if (a == "--merge-fan" && i + 1 < argc) mergeFan = std::stoi(argv[++i]);
        else {
            if (rank == 0) usage(argv[0]);
            MPI_Finalize();
            return 1;
        }
    }

    if (chunkMb == 0) {
        if (rank == 0) std::cerr << "[WARN] --chunk-mb 0 non valido, imposto a 1\n";
        chunkMb = 1;
    }

    omp_set_num_threads(nThreads);
    const size_t chunkBytes = chunkMb * 1024ULL * 1024ULL;

    // ── Directory temporanea per rank ─────────────────────────────────────────
    // Ogni rank usa una sotto-directory distinta per evitare collisioni di nomi
    // quando più rank girano sullo stesso nodo (shared-memory node).
    TempDir workTmp(tmpDir, "spm_mpi_r" + std::to_string(rank));
    const std::string myTmp = workTmp.str();

    if (rank == 0) {
        std::cout << "=== MPI+OMP MergeSort out-of-core ===\n"
                  << "  ranks    : " << numProcs << "\n"
                  << "  threads  : " << nThreads << "\n"
                  << "  chunk    : " << chunkMb  << " MB\n"
                  << "  fan-in   : " << mergeFan << "\n"
                  << "  tmp base : " << tmpDir   << "\n\n";
    }

    // ── Sincronizzazione iniziale ─────────────────────────────────────────────
    // La barriera assicura che tutti i rank abbiano creato la propria tmp dir
    // e stampato prima di iniziare.
    MPI_Barrier(MPI_COMM_WORLD);
    double tStart = wall();

    // =========================================================================
    // FASE 1: Sort locale per stripe
    // =========================================================================
    double t1a = wall();

    // Ogni rank chiama fileSize: syscall veloce (stat), nessun I/O del file.
    int64_t totalBytes = fileSize(inputPath);
    if (totalBytes < 0) {
        std::cerr << "[rank " << rank << "] impossibile aprire " << inputPath << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // boundaries[r] = primo byte della stripe del rank r.
    // Solo rank 0 calcola i boundary con una scansione sequenziale del file;
    // poi MPI_Bcast li diffonde a tutti in O(log P) passi.
    std::vector<int64_t> boundaries(numProcs + 1, 0);
    if (rank == 0) {
        boundaries = computeRecordBoundaries(inputPath, totalBytes, numProcs);
    }
    MPI_Bcast(boundaries.data(), numProcs + 1, MPI_INT64_T, 0, MPI_COMM_WORLD);

    // Ogni rank conosce ora esattamente il proprio intervallo [myStart, myEnd).
    const int64_t myStart = boundaries[rank];
    const int64_t myEnd   = boundaries[rank + 1];

    const std::string localSorted = myTmp + "/local_sorted.bin";

    {
        // sortRangeToRuns legge solo la stripe [myStart, myEnd) e produce
        // file run_N.bin ordinati nella directory temporanea del rank.
        std::vector<std::string> runPaths =
            sortRangeToRuns(inputPath, myTmp, chunkBytes, myStart, myEnd);

        if (runPaths.empty()) {
            // Stripe vuota (può succedere se il file ha meno record di P).
            // Creo un file vuoto per uniformità: la fase 2 lo gestirà.
            FILE* f = std::fopen(localSorted.c_str(), "wb");
            if (f) std::fclose(f);

        } else if (runPaths.size() == 1) {
            // Una sola run: rinomino direttamente (O(1), nessuna copia).
            if (std::rename(runPaths[0].c_str(), localSorted.c_str()) != 0)
                throw std::runtime_error("mpi_sort: rename local_sorted fallito");

        } else {
            // Più run: K-way merge locale seriale.
            // Scelta "seriale" (parallelMerge=false): più rank girano sullo
            // stesso nodo e saturerebbero il bus disco se facessero merge
            // parallelo contemporaneamente.
            kwayMerge(runPaths, localSorted, mergeFan,
                      /*deleteRuns=*/true, /*parallelMerge=*/false);
        }
    }

    // La barriera garantisce che tutti i rank abbiano completato la fase 1
    // prima di misurare il tempo e iniziare la fase 2.
    MPI_Barrier(MPI_COMM_WORLD);
    double t1b = wall();
    if (rank == 0)
        std::cout << "Fase 1 (sort locale): " << (t1b - t1a) << " s\n";

    // =========================================================================
    // FASE 2: Binary tree merge distribuito
    // =========================================================================
    //
    // Schema (P=8, passo per passo):
    //
    //   step=1: rank1→rank0  rank3→rank2  rank5→rank4  rank7→rank6
    //   step=2: rank2→rank0  rank6→rank4
    //   step=4: rank4→rank0
    //
    // Dopo log2(P) step, rank 0 ha il file completamente ordinato.
    //
    // Ogni rank partecipa ad al più un ruolo per step:
    //   isSender   → invia il suo file corrente e poi si ferma
    //   isReceiver → riceve, fonde (2-way merge), aggiorna il file corrente
    //   (altri)    → si fermano già nei passi precedenti
    //
    double t2a = wall();
    std::string currentFile = localSorted; // File che "rappresenta" questo rank.

    for (int step = 1; step < numProcs; step *= 2) {
        const int groupSize = step * 2; // Dimensione del gruppo a questo step.

        // myGroup è il rank del capofila (receiver) del mio gruppo.
        const int myGroup = (rank / groupSize) * groupSize;

        // Il capofila del gruppo (rank % groupSize == 0) raccoglie i dati.
        const bool isReceiver = (rank % groupSize == 0);

        // Il "partner" del capofila (rank % groupSize == step) invia i dati.
        const bool isSender   = (rank % groupSize == step);

        // Tutti gli altri rank non partecipano a questo step: si limitano
        // ad attendere insieme gli altri alla barriera implicita del loop.
        if (!isReceiver && !isSender) continue;

        if (isSender) {
            // ── Sender ────────────────────────────────────────────────────────
            // Tag univoci per step: evitano che messaggi di step diversi
            // si mescolino anche in caso di implementazioni MPI permissive.
            const int tagSize = 100 + step;
            const int tagData = 200 + step;

            mpiSendFile(currentFile, myGroup, tagSize, tagData, MPI_COMM_WORLD);

            // Il sender non partecipa più ai passi successivi: cancella
            // il file locale per liberare spazio disco.
            if (std::remove(currentFile.c_str()) != 0)
                std::fprintf(stderr, "[WARN] rank %d: impossibile rimuovere %s\n",
                             rank, currentFile.c_str());
            currentFile = ""; // Segnala che questo rank ha terminato.

        } else { // isReceiver
            // ── Receiver ──────────────────────────────────────────────────────
            const int sender = rank + step;

            // Controllo necessario quando P non è una potenza di due:
            // il partner potrebbe non esistere.
            if (sender < numProcs) {
                const int tagSize = 100 + step;
                const int tagData = 200 + step;

                // Ricevo il file del sender su disco locale.
                const std::string recvPath =
                    myTmp + "/recv_step" + std::to_string(step) + ".bin";
                mpiRecvFile(recvPath, sender, tagSize, tagData, MPI_COMM_WORLD);

                // Merge 2-way out-of-core tra il mio file corrente e quello ricevuto.
                // Il risultato diventa il nuovo "file corrente".
                const std::string mergedPath =
                    myTmp + "/merged_step" + std::to_string(step) + ".bin";

                kwayMerge({currentFile, recvPath}, mergedPath,
                          /*mergeFan=*/2,
                          /*deleteRuns=*/true,  // Cancella entrambi gli input dopo il merge.
                          /*parallelMerge=*/false);

                currentFile = mergedPath;
            }
            // Se sender >= numProcs il receiver non ha partner: mantiene
            // il proprio file corrente invariato e passa allo step successivo.
        }
    }

    double t2b = wall();
    if (rank == 0)
        std::cout << "Fase 2 (merge distribuito): " << (t2b - t2a) << " s\n";

    // ── Output finale (solo rank 0) ───────────────────────────────────────────
    // Rank 0 è l'unico che ha partecipato a tutti gli step come receiver,
    // quindi currentFile contiene l'intero file ordinato.
    if (rank == 0) {
        // rename è atomico: se currentFile e outputPath sono sullo stesso
        // filesystem, non c'è nessuna copia di dati.
        if (currentFile != outputPath) {
            if (std::rename(currentFile.c_str(), outputPath.c_str()) != 0)
                throw std::runtime_error("mpi_sort: rename output finale fallito");
        }

        double tEnd = wall();
        std::cout << "\n--- Riepilogo tempi (rank 0) ---\n"
                  << "  Sort locale  (Fase 1) : " << (t1b - t1a)      << " s\n"
                  << "  Merge dist.  (Fase 2) : " << (t2b - t2a)      << " s\n"
                  << "  Totale               : " << (tEnd - tStart) << " s\n";
    }

    MPI_Finalize();
    return 0;
}
