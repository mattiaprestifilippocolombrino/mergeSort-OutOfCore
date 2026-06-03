// =============================================================================
// mpi_sort.cpp  –  MergeSort out-of-core distribuito (MPI + OpenMP)
// =============================================================================
//
// Questo file contiene l'orchestratore della versione distribuita del progetto:
// non implementa direttamente il confronto tra record o il merge elementare,
// ma coordina i moduli comuni (`chunk_sorter`, `kway_merger`, `record`) su piu'
// processi MPI.  L'obiettivo e' ordinare file binari piu' grandi della RAM.
//
// Formato dei record gestito dai moduli common:
//   [key: uint64_t, 8 byte][len: uint32_t, 4 byte][payload: len byte]
//
// L'ordinamento e' per `key`.  Il payload viene trasportato insieme alla key,
// ma non viene interpretato da questo file: qui interessa solo preservare
// record completi e non tagliarli mai durante la divisione tra rank.
//
// Utilizzo:
//   mpirun -n P ./mpi_sort <input> <output> [opzioni]
//
//   --chunk-mb  N     Dimensione del blocco in RAM per ogni run locale (default: 256 MB)
//   --threads   N     Numero di thread OpenMP per rank (default: max hw)
//   --tmp-dir   PATH  Directory per i file temporanei (default: /scratch)
//   --merge-fan N     Fan-in massimo del K-way merge locale (default: 64)
//   --multipass-local-merge
//                     Usa il merge locale multi-pass semplice dentro ogni rank (default)
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
// Lettura guidata del file:
//   1. Preparazione MPI/OpenMP e directory temporanee per rank.
//   2. Calcolo delle stripe sicure sul file di input.
//   3. Sort locale out-of-core di ogni stripe.
//   4. Merge distribuito ad albero fino a concentrare tutto su rank 0.
//   5. Rinomina atomica del risultato finale nel path richiesto.
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

// Moduli del progetto:
//   chunk_sorter.hpp  -> spezza una stripe in chunk, li ordina e produce run.
//   kway_merger.hpp   -> fonde run gia' ordinate in modo out-of-core.
//   temp_dir.hpp      -> crea e pulisce directory temporanee RAII per rank.
#include "chunk_sorter.hpp"
#include "kway_merger.hpp"
#include "temp_dir.hpp"

// Librerie di parallelismo e sistema:
//   MPI gestisce comunicazione tra processi/rank.
//   OpenMP gestisce il parallelismo intra-rank durante il sort locale.
#include <mpi.h>
#include <omp.h>

// Librerie standard e POSIX usate per parsing, I/O C-style e stat del file.
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

/*
Funzione che restituisce il tempo attuale in secondi utilizzando il timer ad alta risoluzione 
di MPI. Viene usata per misurare le prestazioni delle varie fasi.
MPI garantisce che questo timer sia sincronizzato tra i vari processi.
*/
static double wall() { return MPI_Wtime(); }

/*
Funzione che stampa a video le istruzioni su come lanciare il programma e quali opzioni 
sono disponibili, per poi terminare l'esecuzione. Viene chiamata tipicamente solo dal rank 0 
se l'utente non inserisce i parametri obbligatori (input e output) da riga di comando.
*/
static void usage(const char* prog) {
    std::cerr << "Utilizzo: mpirun -n P " << prog << " <input> <output> [opzioni]\n"
              << "  --chunk-mb  N     MB per chunk locale (default: 256)\n"
              << "  --threads   N     Thread OpenMP per rank (default: max hw)\n"
              << "  --tmp-dir   PATH  Directory temporanea (default: /scratch)\n"
              << "  --merge-fan N     Fan-in per merge locale multi-pass (default: 64)\n"
              << "  --multipass-local-merge\n"
              << "                    Usa il merge locale multi-pass semplice dentro ogni rank (default)\n";
    std::exit(1);
}

/*
Funzione che calcola e restituisce la dimensione in byte di un file specificato dal 'path'.
Utilizza la funzione di sistema POSIX 'stat', che legge i metadati del file 
senza dover aprire il contenuto. 
Restituisce int64_t invece di size_t per evitare problemi di overflow
con file molto grandi (> 4 GB) su sistemi a 32-bit. In caso di errore (es. file 
non trovato), restituisce -1 in modo che il chiamante possa decidere se abortire.
*/
static int64_t fileSize(const std::string& path) {
    struct stat st{};    // Inizializza una struct stat a zero
    if (::stat(path.c_str(), &st) != 0) return -1;  //Viene eseguita la funzione stat sul path passato, e viene salvata la dimensione del file in st.st_size. Se fallisce, restituisce -1.
    return static_cast<int64_t>(st.st_size);
}

/*
Funzione che suddivide il file di input in numProcs parti di dimensioni simili, 
assicurandosi che ogni divisione cada ESATTAMENTE all'inizio di un record.
Se dividessimo il file in modo matematico (es. totalSize / numProcs), 
rischieremmo di tagliare un record a metà, corrompendo l'ordinamento.
Algoritmo:
1. Scorre il file sequenzialmente, leggendo l'header e ignorando il payload.
2. Quando l'offset corrente supera la dimensione teorica della partizione, si ferma
   e salva quell'offset come punto di divisione per il rank successivo.
Restituisce un vettore 'boundaries' di numProcs + 1 offset dove:
- boundaries[0] = 0 (inizio del file)
- boundaries[i] = byte di inizio della partizione assegnata al rank 'i'
- boundaries[numProcs] = dimensione totale del file (fine del file)
*/
static std::vector<int64_t> computeRecordBoundaries(
    const std::string& path,       //path del file da partizionare
    int64_t            totalSize,    //dimensione totale del file
    int                numProcs)     //numero di processi MPI da usare
{
     

    // Si crea il vettore dei boundary, che conterrà per ogni processo il punto di inizio della sua stripe.
    //Si inizializza tutti i boundary a totalSize (fine del file). In tal modo i rank con stripe vuota manterranno questo valore → stripe [totalSize, totalSize).
    std::vector<int64_t> boundaries(numProcs + 1, totalSize);

    //Si inizializza il primo e l'ultimo boundary, rispettivamente a 0 e totalSize. 
    boundaries[0]        = 0;  
    boundaries[numProcs] = totalSize;

    // Si apre il file in modalità lettura binaria.
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("computeRecordBoundaries: impossibile aprire " + path); // Se non si riesce ad aprire il file, viene lanciata un'eccezione.

    try {
        // Viene allocato un buffer di 4MB per ridurre le syscall durante la scansione sequenziale.
        std::setvbuf(f, nullptr, _IOFBF, 4 * 1024 * 1024); 

        int nextRank = 1;  // Variabile che tiene traccia del prossimo rank da processare. Si inizializza al rank 1.

        // Ciclo utilizzato per calcolare i boundary di ogni rank. Itera fino a quando il prossimo rank da processare non sarà < numProcs. 
        while (nextRank < numProcs) {
            off_t pos = ftello(f);  // Restituisce l'offset corrente del file. pos sarà sempre l'inizio di un record, perchè nelle letture leggiamo l'header e skippiamo il payload.
            if (pos < 0 || pos >= static_cast<off_t>(totalSize)) break;  // Se l'offset corrente è negativo o maggiore-uguale alla dimensione totale del file, interrompe il ciclo.

            // Si calcola il target teorico per il rank nextRank, usando la divisione uniforme in byte.
            int64_t target = (totalSize * static_cast<int64_t>(nextRank)) / numProcs;

            // Se il target è stato raggiunto o superato dalla posizione attuale del file, si salva il boundary corrente come posizione di inizio del rank nextRank 
            // e si incrementa il rank da processare. Si passa poi alla prossima iterazione del ciclo.
            if (static_cast<int64_t>(pos) >= target) {  
                boundaries[nextRank] = static_cast<int64_t>(pos);  
                ++nextRank; 
                continue;  
            }

            // Se la posizione del file non è arrivata ancora al target, leggiamo un header di record e skippiamo il payload, in modo da avanzare alla posizione del record successivo nel file.
            RecordHeader hdr;
            if (!readHeader(f, hdr)) break; // EOF inatteso
            skipPayload(f, hdr.len);
        }

        std::fclose(f);  // Chiude il file. 
    } catch (...) {
        std::fclose(f);   // Nel caso venga lanciata un'eccezione, il file viene comunque chiuso prima di rilanciare l'eccezione. 
        throw;
    }
    return boundaries; // Restituisce il vettore dei boundary.
}


//Dimensione del blocco di trasferimento (64 MB), usati per bufferizzare i dati prima di inviarli via MPI.
//Se si usano blocchi troppo piccoli, l'overhead di MPI sarà troppo alto. 
//Se si usano blocchi troppo grandi, ci sarà meno overlap tra disco e rete. 
//Viene scelto 64 MB per bilanciare i due aspetti. Viene mantenuto sotto INT_MAX affinché MPI_Isend/Irecv possano ricevere il count come int.
static constexpr size_t PIPE_CHUNK = 64ULL * 1024 * 1024;

/*
Funzione che invia un intero file dal rank corrente a un rank destinazione via MPI.
Implementa l'ottimizzazione "Pipelining a Doppio Buffer": dividendo il file in blocchi di dimensione PIPE_CHUNK, 
mentre il blocco 'i' viene inviato in modo asincrono (MPI_Isend), 
la CPU legge contemporaneamente il blocco 'i+1' dal disco. L'uso di due buffer alternati permette 
di sovrapporre i tempi di I/O ai tempi di trasferimento di rete, massimizzando le prestazioni.
Prima di inviare i dati grezzi, comunica la dimensione totale del file,
in modo che il ricevente sappia quanti byte aspettarsi in arrivo.
*/
static void mpiSendFile(
    const std::string& path, // path del file locale da inviare
    int dest,                // rank di destinazione
    int tagSize,             // tag MPI per il messaggio che comunica la dimensione
    int tagData,             // tag MPI per i messaggi che contengono i dati
    MPI_Comm comm)           // comunicatore MPI da usare
{
    // Calcola la dimensione totale del file da inviare
    int64_t sz = fileSize(path);
    // Invia la dimensione al ricevente, in modo che sappia quando fermarsi
    MPI_Send(&sz, 1, MPI_INT64_T, dest, tagSize, comm); //Il size viene comunicato in modo sincrono per evitare race condition tra i vari sender.

    if (sz <= 0) return; // Se il file è vuoto, la funzione termina e non c'è nulla da inviare

    // Si apre il file in lettura binaria
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("mpiSendFile: impossibile aprire " + path); // Lancia eccezione se apertura fallisce

    // Creazione dei due buffer alternati per implementare il pipelining
    std::vector<char> bufA(PIPE_CHUNK);
    std::vector<char> bufB(PIPE_CHUNK);
    char* sendBuf = bufA.data(); // Puntatore al buffer usato per essere spedito tramite MPI.
    char* readBuf = bufB.data(); // Puntatore al buffer usato per contenere i dati letti dal disco.

    int64_t remaining = sz; // Quantità di byte che restano ancora da leggere dal disco. Si inizializza alla dimensione del file.
    MPI_Request req = MPI_REQUEST_NULL; // Handle per tracciare la richiesta MPI asincrona.

    // Legge il primo blocco dal disco prima di entrare nel ciclo, riempiendo sendBuf
    size_t firstBatch = static_cast<size_t>(std::min(static_cast<int64_t>(PIPE_CHUNK), remaining));  //Si calcola la dimensione del primo blocco da inviare
    if (std::fread(sendBuf, 1, firstBatch, f) != firstBatch)  // Si legge il primo blocco dal disco e lo si memorizza in sendBuf.
        throw std::runtime_error("mpiSendFile: lettura troncata (primo blocco)");
    remaining -= static_cast<int64_t>(firstBatch); // Si aggiorna il numero di byte rimasti da leggere.

    size_t pendingSize = firstBatch; // Dimensione del blocco che è pronto per essere inviato

    // Ciclo di invio dei blocchi a doppio buffer. Finchè ci sono dati da inviare, si continua con l'invio e la lettura.
    while (pendingSize > 0) {
        // Invia in modo asincrono i byte contenuti in sendBuf al destinatario specificato
        MPI_Isend(sendBuf, static_cast<int>(pendingSize), MPI_BYTE, dest, tagData, comm, &req);

        size_t nextBatch = 0;  //Salva la dimensione del blocco da leggere nel prossimo ciclo

        // Se ci sono ancora byte da leggere dal disco. Il blocco successivo viene letto dal disco e memorizzato in readBuf, mentre l'invio del blocco corrente procede in background
        if (remaining > 0) { 
            nextBatch = static_cast<size_t>(std::min(static_cast<int64_t>(PIPE_CHUNK), remaining));   // Calcola la dimensione del blocco da leggere nel prossimo ciclo
            if (std::fread(readBuf, 1, nextBatch, f) != nextBatch) // Legge il blocco dal disco e lo memorizza in readBuf
                throw std::runtime_error("mpiSendFile: lettura troncata");
            remaining -= static_cast<int64_t>(nextBatch); // Aggiorna i byte ancora da leggere
        }

        // Attende in modo bloccante che l'invio asincrono precedente sia completamente terminato
        MPI_Wait(&req, MPI_STATUS_IGNORE);

        // Scambia i ruoli dei due buffer: il readBuf appena riempito diventa il sendBuf per il giro successivo,
        // e il sendBuf appena inviato si libera per ricevere nuove letture
        std::swap(sendBuf, readBuf);
        pendingSize = nextBatch; // Aggiorna la dimensione da inviare al giro successivo
    }

    std::fclose(f); // Chiude il file
}


/*
Funzione che riceve un file inviato da 'mpiSendFile' e lo salva su disco locale.
Sfrutta il "Pipelining a Doppio Buffer" per nascondere le latenze di scrittura su disco.
Riceve prima la dimensione totale del file, dopodiché cicla:
1. Attende che la ricezione del blocco corrente sia completata (MPI_Wait).
2. Lancia la ricezione del blocco successivo in background (MPI_Irecv).
3. Scrive su disco il blocco appena ricevuto.
*/
static void mpiRecvFile(
    const std::string& path, // path locale in cui salvare il file ricevuto
    int src,                 // rank mittente
    int tagSize,             // tag MPI usato per il messaggio iniziale (la dimensione)
    int tagData,             // tag MPI usato per i messaggi contenenti dati
    MPI_Comm comm)           // comunicatore MPI da usare
{
    // Riceve dal mittente la dimensione totale del file da processare
    int64_t sz;
    MPI_Recv(&sz, 1, MPI_INT64_T, src, tagSize, comm, MPI_STATUS_IGNORE);

    if (sz <= 0) {
        // Se la dimensione è <= 0, creo comunque un file vuoto.
        // Il merge successivo riceve sempre un path valido, anche quando
        // un rank aveva una stripe vuota.
        FILE* empty = std::fopen(path.c_str(), "wb");
        if (!empty) throw std::runtime_error("mpiRecvFile: impossibile creare " + path);
        std::fclose(empty);
        return;
    }

    // Apre il file in scrittura binaria per riversarci i dati in arrivo
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("mpiRecvFile: impossibile creare " + path); // Gestione errore

    // Imposta un buffer largo (8MB) lato file system per ottimizzare le scritture
    std::setvbuf(f, nullptr, _IOFBF, 8 * 1024 * 1024);

    // Creazione dei due buffer alternati
    std::vector<char> bufA(PIPE_CHUNK);
    std::vector<char> bufB(PIPE_CHUNK);
    char* recvBuf  = bufA.data(); // Puntatore al buffer in cui scrivere i dati in arrivo dalla rete
    char* writeBuf = bufB.data(); // Puntatore al buffer da cui leggere i dati per scriverli su disco

    int64_t remaining = sz; // Contatore dei byte che si aspetta ancora di ricevere
    MPI_Request req = MPI_REQUEST_NULL; // Handle della richiesta MPI asincrona

    // Ricezione asincrona del primo blocco
    size_t firstBatch = static_cast<size_t>(std::min(static_cast<int64_t>(PIPE_CHUNK), remaining));   // Si calcola la dimensione del primo blocco da ricevere
    MPI_Irecv(recvBuf, static_cast<int>(firstBatch), MPI_BYTE, src, tagData, comm, &req); // Si riceve in modo asincrono il primo blocco dal mittente
    remaining -= static_cast<int64_t>(firstBatch); // Si aggiorna la quantità di byte ancora da ricevere
    
    size_t pendingSize = firstBatch; // Dimensione del blocco che si sta aspettando di ricevere dalla rete

    // Ciclo di ricezione a doppio buffer. Si continua finché ci sono dati da ricevere
    while (pendingSize > 0) {
        // Attende in modo bloccante che la ricezione del blocco corrente sulla rete sia completata
        MPI_Wait(&req, MPI_STATUS_IGNORE);
        
        // Scambia i ruoli dei buffer: il recvBuf pieno diventa writeBuf per essere scritto su disco.
        // il writeBuf si libera, diventando il nuovo recvBuf, pronto per ricevere nuovi dati dalla rete.
        std::swap(recvBuf, writeBuf);

        size_t nextBatch = 0;  // Dimensione del blocco da leggere nel prossimo ciclo

        if (remaining > 0) { // Se ci sono ancora byte da leggere
            // Lancia subito la ricezione asincrona del blocco successivo prima di mettersi a scrivere su disco
            nextBatch = static_cast<size_t>(std::min(static_cast<int64_t>(PIPE_CHUNK), remaining));  // Si calcola la dimensione del blocco da leggere nel prossimo ciclo
            MPI_Irecv(recvBuf, static_cast<int>(nextBatch), MPI_BYTE, src, tagData, comm, &req); // Si riceve in modo asincrono il blocco dal mittente
            remaining -= static_cast<int64_t>(nextBatch); // Aggiorna i byte rimanenti da ricevere
        }

        // Mentre la rete riceve in background il nuovo blocco, la CPU scrive su disco il blocco precedente 
        if (std::fwrite(writeBuf, 1, pendingSize, f) != pendingSize)  // Scrittura del blocco da writeBuf su disco 
            throw std::runtime_error("mpiRecvFile: scrittura fallita");

        pendingSize = nextBatch; // Si aggiorna la dimensione dei dati da ricevere attesa, per il prossimo ciclo
    }

    std::fclose(f); // Chiude il file
}

/*
Main che funge da orchestratore MPI del programma distribuito. Viene eseguito in parallelo da tutti
i processi MPI avviati.
1. Inizializzazione: configura MPI e OpenMP, esegue il parsing dei parametri e crea 
   una directory temporanea per ogni rank.
2. Suddivisione e Sort Locale (Fase 1): il rank 0 analizza il file di input per trovare 
   punti di divisione sicuri e li diffonde (MPI_Bcast). Ogni rank legge la propria stripe, 
   la ordina a chunk con OpenMP, ed esegue un k-way merge locale.
3. Merge Distribuito (Fase 2): Usa un "Albero Binario" (Binary Tree) per fondere le 
   porzioni ordinate dei singoli rank. A ogni step, i rank "Sender" inviano i dati 
   ed escono; i rank "Receiver" ricevono, fondono i dati in locale (2-way merge), 
   e passano allo step successivo.
Dopo log2(N) iterazioni, il rank 0 avrà il file completamente ordinato.
*/
int main(
    int argc,     
    char* argv[])
{
    int provided;  // Salva il livello di supporto threading di MPI
    // Inizializza MPI indicando che il processo usa thread, ma solo il main thread fa chiamate MPI
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    if (provided < MPI_THREAD_FUNNELED) { // Se MPI non supporta MPI_THREAD_FUNNELED, si segnala che si usa provided
        std::cerr << "[WARN] MPI non supporta MPI_THREAD_FUNNELED, continuo con " << provided << "\n";
    }

    int rank, numProcs;  // ID del processo corrente e numero totale di processi MPI avviati
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);     // Ottiene l'id del processo corrente (da 0 a numProcs-1)
    MPI_Comm_size(MPI_COMM_WORLD, &numProcs); // Ottiene il numero totale di processi MPI avviati

    // Se mancano gli argomenti essenziali (file di input, output), il rank 0 stampa l'uso e tutti terminano.
    if (argc < 3) {
        if (rank == 0) usage(argv[0]);
        MPI_Finalize();
        return 1;
    }

    try {

    // Estrae i parametri base passati dall'utente
    std::string inputPath  = argv[1]; // File di input da ordinare
    std::string outputPath = argv[2]; // File di output
    
    // Variabili per i parametri opzionali configurabili con flag (e relativi default)
    std::string tmpDir     = "/scratch";  // Directory base per i file temporanei
    size_t      chunkMb    = 256;     // Dimensione massima in MB per ogni chunk da ordinare localmente
    int         nThreads   = omp_get_max_threads(); // Numero di thread OpenMP da usare
    int         mergeFan   = 64;      // Fan-in del merge locale multi-pass

    // Ciclo di parsing dei flag opzionali e assegnamento
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--chunk-mb"  && i + 1 < argc) chunkMb  = std::stoul(argv[++i]);
        else if (a == "--threads"   && i + 1 < argc) nThreads = std::stoi(argv[++i]);
        else if (a == "--tmp-dir"   && i + 1 < argc) tmpDir   = argv[++i];
        else if (a == "--merge-fan" && i + 1 < argc) {
            mergeFan = std::stoi(argv[++i]);
        }
        else if (a == "--multipass-local-merge") {
            // Default esplicito, accettato per compatibilita' con gli script.
        }
        else {
            if (rank == 0) usage(argv[0]);
            MPI_Finalize();
            return 1;
        }
    }

    // Normalizza chunkMb per evitare 0
    if (chunkMb == 0) {
        if (rank == 0) std::cerr << "[WARN] --chunk-mb 0 non valido, imposto a 1\n";
        chunkMb = 1;
    }

    omp_set_num_threads(nThreads);  // Imposta il numero di thread OpenMP da usare per questo processo
    const size_t chunkBytes = chunkMb * 1024ULL * 1024ULL; // Converte la dimensione del chunk da MB a byte

    // Crea in RAII una sotto-directory temporanea esclusiva del rank per isolare l'I/O ed evitare collisioni.
    // L'oggetto TempDir rimuove la cartella e i suoi contenuti automaticamente alla distruzione
    TempDir workTmp(tmpDir, "spm_mpi_r" + std::to_string(rank));  // Crea una cartella temporanea per il rank
    const std::string myTmp = workTmp.str();

    // Il rank master stampa un resoconto delle configurazioni usate
    if (rank == 0) {
        std::cout << "=== MPI+OMP MergeSort out-of-core ===\n"
                  << "  ranks    : " << numProcs << "\n"
                  << "  threads  : " << nThreads << "\n"
                  << "  chunk    : " << chunkMb  << " MB\n"
                  << "  local merge : mpi-local-multipass\n"
                  << "  fan-in   : " << mergeFan << "\n"
                  << "  tmp base : " << tmpDir   << "\n\n";
    }

    // Barriera MPI per garantire che tutti i processi abbiano preparato le risorse e le directory prima di procedere.
    MPI_Barrier(MPI_COMM_WORLD);  // Tutte le MPI si bloccano qui finché non arrivano tutte
    double tStart = wall(); // Avvia il cronometro totale

    // =========================================================================
    // FASE 1: Sort locale per stripe
    // =========================================================================
    double t1a = wall(); // Avvia il cronometro per la Fase 1

    // Verifica globale rapida che il file esista per evitare stalli MPI
    int64_t totalBytes = fileSize(inputPath);
    if (totalBytes < 0) {
        std::cerr << "[rank " << rank << "] impossibile aprire " << inputPath << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1); // Fornisce errore ed esce forzatamente se un processo fallisce qui
    }

    // Vettore per memorizzare le porzione di file di cui ogni processo si occupa.
    std::vector<int64_t> boundaries(numProcs + 1, 0);  
    // Il rank 0 legge l'input in modo seriale per calcolare correttamente i limiti dei vari record.
    if (rank == 0) {
        boundaries = computeRecordBoundaries(inputPath, totalBytes, numProcs);
    }
    
    // Il rank 0 diffonde a tutti i rank i boundaries, così tutti sapranno la loro precisa porzione di file.
    MPI_Bcast(boundaries.data(), numProcs + 1, MPI_INT64_T, 0, MPI_COMM_WORLD);  // Rank 0 invia, tutti ricevono i boundary

    // Ogni rank cattura l'offset di inizio e fine in byte della sua parte esclusiva di file [myStart, myEnd)
    const int64_t myStart = boundaries[rank];  // Offset di inizio del rank
    const int64_t myEnd   = boundaries[rank + 1];  // Offset di fine del rank

    // File in cui il processo salva il risultato finale del sort locale
    const std::string localSorted = myTmp + "/local_sorted.bin";

    {
        // Viene chiamata la funzione che partiziona internamente la stripe, ordina le run e le scrive su disco, usando openmp.
        // Viene ritornato un vettore di stringhe contenente i path dei file (run) dei chunk ordinati.
        std::vector<std::string> runPaths =
            sortRangeToRuns(inputPath, myTmp, chunkBytes, myStart, myEnd);

        // Se la porzione risulta vuota
        if (runPaths.empty()) {
            // Crea un file vuoto come placeholder e lo chiude. Verrà ignorato fluidamente nella Fase 2.
            FILE* f = std::fopen(localSorted.c_str(), "wb");
            if (!f) {
                throw std::runtime_error("mpi_sort: impossibile creare local_sorted vuoto");
            }
            std::fclose(f);

        } else if (runPaths.size() == 1) {
            if (std::rename(runPaths[0].c_str(), localSorted.c_str()) != 0)
                throw std::runtime_error("mpi_sort: rename local_sorted fallito");

        } else {
            // Multi-pass semplice: default finale per il merge locale.
            kwayMerge(runPaths, localSorted, mergeFan,
                      /*deleteRuns=*/true, /*parallelMerge=*/false);
        }
    }

    // Barriera in cui tutti i rank attendono che tutti abbiano la stripe locale pronta prima di iniziare lo scambio di dati tra rank.
    MPI_Barrier(MPI_COMM_WORLD);
    double t1b = wall(); // Ferma il cronometro della Fase 1
    if (rank == 0)
        std::cout << "Fase 1 (sort locale): " << (t1b - t1a) << " s\n";  //Il rank 0 stampa il tempo impiegato per la fase 1


    /* =========================================================================
       FASE 2: Binary tree merge distribuito: La Fase 1 ha già prodotto, su ogni processo MPI, un file locale ordinato:
       Quindi all’inizio ogni rank possiede il proprio pezzo ordinato del dataset.
       L’obiettivo della Fase 2 è fondere tutti questi file ordinati fino ad ottenere un unico file ordinato finale, che sarà posseduto dal rank 0.
       Il merge avviene come un albero binario.
       Esempio con 8 processi:
       Step 1:
       rank 1 -> rank 0
       rank 3 -> rank 2
       rank 5 -> rank 4
       rank 7 -> rank 6
       
       Step 2:
       rank 2 -> rank 0
       rank 6 -> rank 4
       
       Step 4:
       rank 4 -> rank 0
       Alla fine:
       rank 0 contiene tutto il dataset ordinato
       Ogni sender, dopo aver inviato il proprio file, esce di fatto dal merge. 
       Ogni receiver invece riceve un file, lo fonde con il proprio, e continua al livello successivo.    
    
       ============================================================================= */
    double t2a = wall(); // Avvia il cronometro per la Fase 2
    
    // File che contiene il risultato parziale del merging, che ad ogni iterazione viene aggiornato. All'inizio sarà solo il file locale ordinato.
    std::string currentFile = localSorted;  //Inizia puntando al file locale ordinato



    /*Loop principale che scorre le iterazioni dell'albero binario.
     step rappresenta la distanza tra receiver e sender.
     Parte da 1, poi diventa 2, poi 4, poi 8, ecc.
     Ad ogni iterazione si raddoppia la dimensione del gruppo di processi che vengono fusi.
    */
    for (int step = 1; step < numProcs; step *= 2) {
        
        /*groupSize indica quanto è grande il gruppo a questo livello. Esempio con numProcs=8:
            Se step = 1, allora: groupSize = 2; I gruppi sono: [0,1] [2,3] [4,5] [6,7]
            Se step = 2, allora: groupSize = 4; I gruppi sono: [0,1,2,3] [4,5,6,7]
            Se step = 4, allora: groupSize = 8; Il gruppo è: [0,1,2,3,4,5,6,7]
        */

        const int groupSize = step * 2; // Dimensione del gruppo in questo step. Ad ogni step il gruppo raddoppia.
        const int myGroup = (rank / groupSize) * groupSize; // primo rank del gruppo del processo attuale, cioè il receiver principale di quel gruppo.
        
        // Assegnazione dei ruoli di "receiver" o "sender" in base all'attuale ID
        // Un processo è receiver se si trova all’inizio del gruppo.
        // Un processo è sender se si trova a distanza step dall'inizio del gruppo (receiver).
        const bool isReceiver = (rank % groupSize == 0);   // Se il resto è 0, allora il rank è un receiver
        const bool isSender   = (rank % groupSize == step); // Se il resto è step, allora il rank è un sender

        // I rank che non sono necessari per un preciso livello vengono skippati.
        if (!isReceiver && !isSender) continue;

        if (isSender) {
            // ── Sender ────────────────────────────────────────────────────────
            // Crea tag specifici per lo step che eviteranno incroci con messaggi spaiati in reti asincrone
            const int tagSize = 100 + step;  // Tag identificativo unico per inviare la size del file in questo step
            const int tagData = 200 + step;  // Tag identificativo unico per inviare il file in questo step

            // Il nodo sender invia in blocco l'intero risultato da lui ottenuto al suo Receiver target, identificato come myGroup.
            mpiSendFile(currentFile, myGroup, tagSize, tagData, MPI_COMM_WORLD);

            // Avendo svolto il suo lavoro, il Sender distrugge i file transitori e termina formalmente la partecipazione
            if (std::remove(currentFile.c_str()) != 0)  // Se non riesce a rimuovere il file, stampa un warning
                std::fprintf(stderr, "[WARN] rank %d: impossibile rimuovere %s\n",
                             rank, currentFile.c_str());
            currentFile = ""; // File svuotato per indicare la terminazione

        } else { // isReceiver
            // ── Receiver ──────────────────────────────────────────────────────
            const int sender = rank + step; // Calcola l'id esatto del sender preposto ad inviare in questo step

            // Se il numProcs totale non è in potenza del 2, il receiver ignora step vuoti perché non esisterebbe alcun sender
            if (sender < numProcs) {
                const int tagSize = 100 + step;  // Usa gli stessi tag usati dal sender per identificare il msg di size
                const int tagData = 200 + step;  // Usa gli stessi tag usati dal sender per identificare il msg di dati

                // Path di destinazione dove salvare temporaneamente il file ricevuto
                const std::string recvPath =
                    myTmp + "/recv_step" + std::to_string(step) + ".bin";
                
                // Ricezione tramite MPI del file inviato dal sender
                mpiRecvFile(recvPath, sender, tagSize, tagData, MPI_COMM_WORLD);

                // Path di destinazione del file che conterrà il risultato del merge
                const std::string mergedPath =
                    myTmp + "/merged_step" + std::to_string(step) + ".bin";

                // Merge 2-way tra il file locale e quello ricevuto via MPI.
                mergePass({currentFile, recvPath}, mergedPath,
                          /*deleteSource=*/true);

                
                currentFile = mergedPath;  //Assegna il path del file appena creato come corrente per il prossimo ciclo
            }
        }
    }

    double t2b = wall(); // Ferma il timer per la Fase 2
    if (rank == 0)
        std::cout << "Fase 2 (merge distribuito): " << (t2b - t2a) << " s\n";

    // ── Output finale (solo rank 0) ───────────────────────────────────────────
    // Il master rank conclude avendo il path del file finale all'interno di currentFile
    if (rank == 0) {
        // Rinomina il currentFile nel file di output
        if (currentFile != outputPath) {
            if (std::rename(currentFile.c_str(), outputPath.c_str()) != 0)
                throw std::runtime_error("mpi_sort: rename output finale fallito");
        }

        // Output del resoconto del timing
        double tEnd = wall();
        std::cout << "\n--- Riepilogo tempi (rank 0) ---\n"
                  << "  Sort locale  (Fase 1) : " << (t1b - t1a)      << " s\n"
                  << "  Merge dist.  (Fase 2) : " << (t2b - t2a)      << " s\n"
                  << "  Totale               : " << (tEnd - tStart) << " s\n";
    }

    // Disabilita MPI, deallocando o sincronizzando gli ultimi processi
    MPI_Finalize();
    return 0;
    } catch (const std::exception& e) {
        std::cerr << "[rank " << rank << "] errore: " << e.what() << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
}
