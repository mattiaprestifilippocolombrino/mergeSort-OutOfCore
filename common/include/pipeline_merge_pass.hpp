#pragma once
// =============================================================================
// pipeline_merge_pass.hpp  —  Primitiva di merge K-way con pipeline I/O asincrona
// =============================================================================
//
// PERCHÉ ESISTE QUESTO MODULO
// ─────────────────────────────────────────────────────────────────────────────
// Il merge K-way tradizionale (mergePass in kway_merger.hpp) ha un difetto
// fondamentale su task I/O-bound: ogni chiamata a `fread` blocca il thread
// finché il disco non risponde. La CPU, durante questa attesa, è idle.
//
// La stessa cosa vale per `fwrite`: mentre il dato viene scritto su disco,
// la CPU non fa nulla.
//
// SOLUZIONE: PIPELINE A TRE STADI + DOUBLE BUFFERING
// ─────────────────────────────────────────────────────────────────────────────
//
//   ┌─────────┐    buffer pieno    ┌─────────┐    buffer pieno    ┌─────────┐
//   │  READER │ ────────────────▶ │  MERGER │ ────────────────▶ │  WRITER │
//   │         │ ◀──────────────── │         │ ◀──────────────── │         │
//   └─────────┘   buffer vuoto    └─────────┘   buffer vuoto    └─────────┘
//       │                                                             │
//   [Disco: K                                                  [Disco: output]
//    file di run]
//
// READER (1 thread):
//   Riempie grandi blocchi RAM da K file di run. Usa double buffering:
//   mentre il Merger consuma il buffer A, il Reader carica il buffer B.
//   Non fa mai attendere il Merger: quando il Merger ha finito un blocco,
//   quello successivo è già in RAM.
//
// MERGER (1 thread):
//   Fa il K-way merge dalla RAM, usando la min-heap su RecordHeader.
//   NON tocca mai il disco. La sua velocità è limitata solo dalla banda
//   di memoria RAM (GB/s >> MB/s del disco). Produce record ordinati
//   in un buffer di output.
//
// WRITER (1 thread):
//   Quando il buffer di output è pieno (WRITE_BLOCK_SIZE), lo scarica su
//   disco con una singola fwrite() sequenziale. La scrittura avviene
//   mentre il Merger riempie già il buffer successivo (double buffering).
//   La dimensione del blocco (max 32MB) è calibrata per non saturare la
//   page cache su cluster HPC (che può causare "node drain").
//
// VOLUME I/O TOTALE: identico al single-thread (1 lettura + 1 scrittura di N byte).
// COSTO CPU EXTRA:   trascurabile (confronti heap << banda I/O).
// MIGLIORAMENTO:     parte della latenza di scrittura viene sovrapposta al
//                    merge; il throughput resta limitato da disco e memoria.
//
// USO
// ─────────────────────────────────────────────────────────────────────────────
// Questa funzione è progettata per essere chiamata da:
//   - omp_kway_merger_pipeline.hpp (versione OpenMP con omp parallel sections)
//   - ff_kway_merger_pipeline.hpp  (versione FastFlow con ff::Pipeline)
// Non dipende da OMP né da FastFlow: usa solo C++17 e <thread>.
//
// =============================================================================

#include "kway_merger.hpp"   // RunReader, HeapEntry, MERGE_BUF_SIZE, HEADER_SIZE

#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>


// =============================================================================
// Parametri di tuning
// =============================================================================

// Dimensione massima del blocco di output scritto in una singola fwrite().
// 32MB: abbastanza grande da essere efficiente (poche syscall), abbastanza
// piccolo da non saturare la page cache su cluster HPC (evita "node drain").
static constexpr size_t WRITE_BLOCK_SIZE = 32ULL * 1024 * 1024;

// Dimensione del blocco di prefetch per ogni file di run.
// Il Reader carica READ_BLOCK_SIZE byte per run prima che il Merger ne abbia
// bisogno. Un blocco più grande riduce le fread, ma usa più RAM.
// RAM lettura: circa K * READ_BLOCK_SIZE per merge task, piu' i buffer di
// output descritti sotto. Con K=32 e READ_BLOCK_SIZE=4MB: circa 128MB.
static constexpr size_t READ_BLOCK_SIZE = 4ULL * 1024 * 1024;


// =============================================================================
// DoubleBuffer: canale SPSC lock-free a due slot
// =============================================================================
//
// Struttura di sincronizzazione tra esattamente 2 thread: un produttore
// (che riempie) e un consumatore (che svuota).
//
// Due slot: mentre il consumatore scrive su disco lo slot A, il produttore puo'
// riempire lo slot B. Lo scambio usa solo atomiche: niente mutex/condvar.
//
// Semantica:
//   - Il produttore chiama produce(data, size): attende uno slot libero,
//     copia i dati, poi pubblica lo slot con store-release.
//   - Il consumatore chiama consume(data, size): attende uno slot pieno,
//     legge il puntatore con load-acquire, scrive su disco e poi libera lo slot.
//   - Il produttore chiama setDone() quando ha finito di produrre.
//   - cancel() sblocca il produttore se il Writer incontra un errore.
// =============================================================================
struct DoubleBuffer {
    enum SlotState : int { Empty = 0, Full = 1 };

    // Due slot di byte: il produttore scrive su uno mentre il consumatore usa
    // l'altro. La proprieta' dello slot e' indicata da states[i].
    std::vector<char> slots[2];
    size_t            slotSizes[2] = {0, 0};
    std::atomic<int>  states[2];

    int writeSlot = 0;  // Usato solo dal produttore.
    int readSlot  = 0;  // Usato solo dal consumatore.

    std::atomic<bool> done;
    std::atomic<bool> cancelled;

    explicit DoubleBuffer(size_t slotSize) {
        slots[0].resize(slotSize);
        slots[1].resize(slotSize);
        states[0].store(Empty, std::memory_order_relaxed);
        states[1].store(Empty, std::memory_order_relaxed);
        done.store(false, std::memory_order_relaxed);
        cancelled.store(false, std::memory_order_relaxed);
    }

    // Chiamata dal PRODUTTORE: consegna un blocco al consumatore.
    // `data` deve puntare a `size` byte validi già in memoria.
    // Attende se entrambi gli slot sono ancora pieni.
    void produce(const char* data, size_t size) {
        if (size > slots[writeSlot].size()) {
            throw std::runtime_error("DoubleBuffer: blocco piu' grande dello slot");
        }

        while (states[writeSlot].load(std::memory_order_acquire) != Empty) {
            if (done.load(std::memory_order_acquire) ||
                cancelled.load(std::memory_order_acquire)) {
                throw std::runtime_error("DoubleBuffer: consumer non disponibile");
            }
            std::this_thread::yield();
        }

        if (done.load(std::memory_order_acquire) ||
            cancelled.load(std::memory_order_acquire)) {
            throw std::runtime_error("DoubleBuffer: consumer non disponibile");
        }

        std::memcpy(slots[writeSlot].data(), data, size);
        slotSizes[writeSlot] = size;

        // Pubblica lo slot pieno: il consumer vede prima dati e size.
        states[writeSlot].store(Full, std::memory_order_release);
        writeSlot = 1 - writeSlot;
    }

    // Chiamata dal PRODUTTORE: segnala che non ci sono più dati.
    void setDone() {
        done.store(true, std::memory_order_release);
    }

    // Chiamata dal CONSUMATORE in caso di errore: sblocca il produttore.
    void cancel() {
        cancelled.store(true, std::memory_order_release);
        done.store(true, std::memory_order_release);
    }

    // Chiamata dal CONSUMATORE: riceve un blocco dal produttore.
    // Restituisce un puntatore allo slot interno: il chiamante deve poi
    // invocare releaseConsumed() quando ha finito di usare il blocco.
    // Restituisce false se il produttore ha segnalato la fine del flusso.
    bool consume(const char*& data, size_t& outSize) {
        while (states[readSlot].load(std::memory_order_acquire) != Full) {
            if (done.load(std::memory_order_acquire) ||
                cancelled.load(std::memory_order_acquire)) {
                outSize = 0;
                data = nullptr;
                return false;
            }
            std::this_thread::yield();
        }

        data = slots[readSlot].data();
        outSize = slotSizes[readSlot];
        return true;
    }

    // Libera lo slot appena consumato.
    void releaseConsumed() {
        slotSizes[readSlot] = 0;
        states[readSlot].store(Empty, std::memory_order_release);
        readSlot = 1 - readSlot;
    }
};


// =============================================================================
// RunBlockReader: reader di un singolo file di run con blocchi grandi
// =============================================================================
//
// A differenza di RunReader (che legge un record alla volta), RunBlockReader
// carica READ_BLOCK_SIZE byte in un buffer interno con una sola fread().
// Il Merger poi scorre il buffer record per record in RAM, senza syscall.
// Quando il buffer è esaurito, la prossima fread() carica il blocco seguente.
//
// Questo è il meccanismo che elimina le molte piccole fread() del merge
// tradizionale sostituendole con poche grandi fread() bufferizzate.
// =============================================================================
struct RunBlockReader {
    FILE*             file;
    std::vector<char> buf;       // Buffer interno (READ_BLOCK_SIZE byte).
    size_t            bufSize;   // Byte validi nel buffer.
    size_t            bufPos;    // Posizione corrente nel buffer.
    bool              exhausted; // True se il file è finito.

    // Campi pubblici per il Merger (record corrente).
    uint64_t currentKey;
    uint32_t currentLen;
    std::vector<char> payload;

    explicit RunBlockReader(const std::string& path)
        : buf(READ_BLOCK_SIZE), bufSize(0), bufPos(0), exhausted(false),
          currentKey(0), currentLen(0)
    {
        file = std::fopen(path.c_str(), "rb");
        if (!file) {
            throw std::runtime_error("RunBlockReader: impossibile aprire " + path);
        }
        // Disabilitiamo il buffer interno di FILE* perché gestiamo noi il buffering.
        std::setvbuf(file, nullptr, _IONBF, 0);
        advance();
    }

    ~RunBlockReader() { if (file) std::fclose(file); }

    RunBlockReader(const RunBlockReader&)            = delete;
    RunBlockReader& operator=(const RunBlockReader&) = delete;

    // Avanza al record successivo: legge dal buffer interno.
    // Se il buffer è esaurito, lo ricarica con una nuova fread() massiccia.
    void advance() {
        // Assicura che il buffer abbia almeno HEADER_SIZE byte disponibili.
        if (!ensureBytes(HEADER_SIZE)) {
            if (bufSize == 0) {
                exhausted = true;
                return;
            }
            throw std::runtime_error("RunBlockReader: header troncato");
        }

        // Legge l'header direttamente dal buffer in RAM.
        std::memcpy(&currentKey, buf.data() + bufPos, sizeof(uint64_t));
        std::memcpy(&currentLen, buf.data() + bufPos + 8, sizeof(uint32_t));
        bufPos += HEADER_SIZE;

        if (currentLen < 8 || currentLen > PAYLOAD_MAX) {
            throw std::runtime_error("RunBlockReader: len fuori da [8, PAYLOAD_MAX]");
        }
        if (currentLen > READ_BLOCK_SIZE) {
            throw std::runtime_error("RunBlockReader: payload piu' grande di READ_BLOCK_SIZE");
        }

        // Assicura che il buffer abbia i byte del payload.
        if (!ensureBytes(currentLen)) {
            throw std::runtime_error("RunBlockReader: payload troncato");
        }

        // Copia il payload dal buffer in RAM (zero syscall).
        payload.resize(currentLen);
        std::memcpy(payload.data(), buf.data() + bufPos, currentLen);
        bufPos += currentLen;
    }

private:
    // Garantisce che ci siano almeno `needed` byte disponibili nel buffer.
    // Se necessario, sposta i dati residui all'inizio e ricarica.
    // Restituisce false se il file è finito e non ci sono abbastanza byte.
    bool ensureBytes(size_t needed) {
        // Bytes ancora disponibili nel buffer.
        size_t available = bufSize - bufPos;

        if (available >= needed) {
            return true; // Già abbastanza byte: nessuna lettura dal disco.
        }

        // Sposta i byte residui all'inizio del buffer (compattazione).
        if (available > 0) {
            std::memmove(buf.data(), buf.data() + bufPos, available);
        }
        bufPos  = 0;
        bufSize = available;

        // Ricarica il buffer con una singola fread() massiccia dal disco.
        size_t toRead = buf.size() - bufSize;
        size_t got    = std::fread(buf.data() + bufSize, 1, toRead, file);
        bufSize += got;

        return bufSize >= needed;
    }
};


// =============================================================================
// WriteBuffer: buffer di output con flush atomico
// =============================================================================
//
// Accumula i record ordinati prodotti dal Merger in un buffer in RAM.
// Quando il buffer raggiunge WRITE_BLOCK_SIZE, lo passa al Writer tramite
// il DoubleBuffer di output e svuota il buffer locale.
//
// Il Merger interagisce con WriteBuffer tramite append(), non con fwrite()
// diretta: è il Writer thread a fare tutte le scritture su disco.
// =============================================================================
struct WriteBuffer {
    std::vector<char> data;
    DoubleBuffer&     outBuf; // Canale verso il Writer thread.

    explicit WriteBuffer(DoubleBuffer& ob) : outBuf(ob) {
        data.reserve(WRITE_BLOCK_SIZE);
    }

    // Aggiunge un record al buffer. Se il buffer è pieno, lo flush al Writer.
    void append(uint64_t key, uint32_t len, const char* payload) {
        size_t recordSize = HEADER_SIZE + len;
        if (data.size() + recordSize > WRITE_BLOCK_SIZE) {
            flush();
        }
        // Serializza header e payload nel buffer locale.
        size_t pos = data.size();
        data.resize(pos + recordSize);
        std::memcpy(data.data() + pos,     &key, sizeof(uint64_t));
        std::memcpy(data.data() + pos + 8, &len, sizeof(uint32_t));
        std::memcpy(data.data() + pos + HEADER_SIZE, payload, len);
    }

    // Invia il contenuto del buffer al Writer e svuota il buffer locale.
    void flush() {
        if (data.empty()) return;
        outBuf.produce(data.data(), data.size());
        data.clear();
    }

    // Chiamato alla fine del merge: flush dei dati residui + segnale di fine.
    void finalize() {
        flush();
        outBuf.setDone();
    }
};


// =============================================================================
// pipelineMergePass — Entry point della pipeline
// =============================================================================
//
// Fonde `group` file di run ordinati in `outputPath` con una pipeline a due
// attori: il thread chiamante fa Reader+Merger, un Writer thread scrive l'output.
//
// Attore 1 (Reader+Merger, thread chiamante):
//   RunBlockReader gestisce la lettura a blocchi in modo trasparente. Il Merger
//   chiama advance() che, quando il buffer RAM è esaurito, fa una fread()
//   bloccante. Il loop di merge resta in RAM tra una ricarica e l'altra.
//
// Attore 2 (Writer, std::thread):
//   Riceve blocchi dal Merger via DoubleBuffer SPSC e li scrive su disco
//   sequenzialmente, sovrapponendo parte della latenza I/O al merge.
//
// I parametri di tuning WRITE_BLOCK_SIZE e READ_BLOCK_SIZE sono in testa al file.
//
// =============================================================================
inline void pipelineMergePass(
    const std::vector<std::string>& group,       // Run da fondere.
    const std::string&              outputPath,  // File di output finale.
    bool                            deleteSource) // Rimuove i file di run dopo il merge.
{
    const int K = static_cast<int>(group.size());

    if (K == 0) return;

    // Caso banale: una sola run, rinomina senza merge.
    if (K == 1) {
        moveOrCopyRun(group[0], outputPath, deleteSource);
        return;
    }

    // ── Canale Merger → Writer ───────────────────────────────────────────────
    // Il Merger produce blocchi di output ordinati. Il Writer li consuma e
    // li scrive su disco. DoubleBuffer coordina i due thread con atomiche.
    DoubleBuffer outputChannel(WRITE_BLOCK_SIZE);

    // ── Thread WRITER ────────────────────────────────────────────────────────
    // Gira in background: aspetta blocchi da outputChannel, li scrive su disco.
    // La scrittura è sempre sequenziale e in blocchi da WRITE_BLOCK_SIZE:
    // efficiente per il filesystem e sicuro per la page cache del cluster.
    std::exception_ptr writerError;
    std::thread writerThread([&]() {
        FILE* fout = std::fopen(outputPath.c_str(), "wb");
        if (!fout) {
            try {
                throw std::runtime_error("pipelineMerge: impossibile creare " + outputPath);
            } catch (...) {
                writerError = std::current_exception();
                outputChannel.cancel();
                return;
            }
        }
        bool closed = false;
        // Scriviamo gia' blocchi grandi da WRITE_BLOCK_SIZE: evitare un altro
        // buffer stdio riduce memoria e copie senza aumentare le syscall.
        std::setvbuf(fout, nullptr, _IONBF, 0);

        try {
            const char* block = nullptr;
            size_t      blockSize;
            // Ciclo: riceve blocchi dal Merger finché il canale è aperto.
            while (outputChannel.consume(block, blockSize)) {
                if (blockSize == 0) break;
                if (std::fwrite(block, 1, blockSize, fout) != blockSize) {
                    throw std::runtime_error("pipelineMerge: fwrite fallita");
                }
                outputChannel.releaseConsumed();
            }
            if (std::fclose(fout) != 0) {
                closed = true;
                throw std::runtime_error("pipelineMerge: fclose output fallita");
            }
            closed = true;
        } catch (...) {
            if (!closed) {
                std::fclose(fout);
            }
            writerError = std::current_exception();
            outputChannel.cancel();
        }
    });

    // ── Thread MERGER (questo thread) ───────────────────────────────────────
    // Apre K RunBlockReader (lettura a blocchi, zero syscall nel merge loop).
    // Esegue il K-way merge con min-heap, scrive su WriteBuffer.
    // Quando il WriteBuffer è pieno, lo passa al Writer tramite outputChannel.
    std::exception_ptr mergerError;
    try {
        // Apre tutti i reader.
        std::vector<std::unique_ptr<RunBlockReader>> readers;
        readers.reserve(K);
        for (int i = 0; i < K; ++i) {
            readers.push_back(std::make_unique<RunBlockReader>(group[i]));
        }

        // Min-heap: estrae sempre il record con la chiave più piccola.
        std::priority_queue<HeapEntry,
                            std::vector<HeapEntry>,
                            std::greater<HeapEntry>> heap;

        for (int i = 0; i < K; ++i) {
            if (!readers[i]->exhausted) {
                heap.push({readers[i]->currentKey, i});
            }
        }

        // Buffer di output: accumula record prima di passarli al Writer.
        WriteBuffer wbuf(outputChannel);

        // Loop principale del K-way merge — ZERO syscall, tutto in RAM.
        while (!heap.empty()) {
            HeapEntry top = heap.top();
            heap.pop();

            RunBlockReader* rr = readers[top.runIdx].get();

            // Aggiunge il record al buffer di output.
            wbuf.append(rr->currentKey, rr->currentLen, rr->payload.data());

            // Avanza il reader: legge il prossimo record dal blocco in RAM
            // (o carica il blocco successivo dal disco se necessario).
            rr->advance();
            if (!rr->exhausted) {
                heap.push({rr->currentKey, top.runIdx});
            }
        }

        // Flush finale: invia i record residui al Writer e chiude il canale.
        wbuf.finalize();

    } catch (...) {
        mergerError = std::current_exception();
        // Segnala al Writer di terminare anche in caso di errore del Merger.
        outputChannel.setDone();
    }

    // Attende che il Writer abbia finito di scrivere tutto su disco.
    writerThread.join();

    // Propaga eventuali eccezioni (prima il Merger, poi il Writer).
    if (mergerError)  std::rethrow_exception(mergerError);
    if (writerError)  std::rethrow_exception(writerError);

    // ── Pulizia: rimuove i file di run sorgente ──────────────────────────────
    if (deleteSource) {
        for (const std::string& path : group) {
            if (std::remove(path.c_str()) != 0) {
                std::fprintf(stderr,
                             "[WARN] pipelineMergePass: impossibile rimuovere %s\n",
                             path.c_str());
            }
        }
    }
}
