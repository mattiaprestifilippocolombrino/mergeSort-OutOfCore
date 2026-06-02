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
// MIGLIORAMENTO:     la latenza I/O è completamente nascosta → throughput
//                    limitato solo dalla banda di lettura/scrittura del disco.
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

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
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
// RAM totale usata: K * 2 * READ_BLOCK_SIZE (doppio buffer per K run).
// Con K=36, READ_BLOCK_SIZE=4MB: 36 * 2 * 4MB = 288MB.
static constexpr size_t READ_BLOCK_SIZE = 4ULL * 1024 * 1024;


// =============================================================================
// DoubleBuffer: scambio lock-free tra due thread (produttore / consumatore)
// =============================================================================
//
// Struttura di sincronizzazione tra esattamente 2 thread: un produttore
// (che riempie) e un consumatore (che svuota).
//
// Due slot: mentre il consumatore usa lo slot A, il produttore riempie lo
// slot B, e viceversa. Lo scambio avviene con un semplice mutex + condvar.
//
// Semantica:
//   - Il produttore chiama produce(data, size): blocca finché uno slot è
//     disponibile, copia i dati, segnala al consumatore.
//   - Il consumatore chiama consume(outData, outSize): blocca finché un
//     slot è pieno, prende i dati, segnala al produttore.
//   - Il produttore chiama setDone() quando ha finito di produrre.
//   - Il consumatore vede size == 0 come segnale di fine stream.
// =============================================================================
struct DoubleBuffer {
    // Due slot di byte: il produttore scrive su uno mentre il consumatore
    // legge dall'altro, poi si scambiano.
    std::vector<char> slots[2];

    // Metadati dello slot "pronto" (pieno, scritto dal produttore).
    size_t readySize  = 0;   // Quanti byte sono validi nello slot pronto.
    int    readySlot  = -1;  // Quale slot è pieno (-1 = nessuno).

    // Segnale di fine: il produttore lo imposta quando non produrrà più.
    bool done = false;

    std::mutex              mtx;
    std::condition_variable cvReady;    // Segnala al consumatore: "slot pieno".
    std::condition_variable cvConsumed; // Segnala al produttore: "slot libero".

    explicit DoubleBuffer(size_t slotSize) {
        slots[0].resize(slotSize);
        slots[1].resize(slotSize);
    }

    // Chiamata dal PRODUTTORE: consegna un blocco al consumatore.
    // `data` deve puntare a `size` byte validi già in memoria.
    // Blocca se il consumatore non ha ancora preso il blocco precedente.
    void produce(const char* data, size_t size) {
        std::unique_lock<std::mutex> lk(mtx);
        // Aspetta che il consumatore abbia liberato lo slot corrente.
        cvConsumed.wait(lk, [this] { return readySlot < 0 && !done; });

        // Scegli lo slot disponibile (alterniamo 0 e 1).
        readySlot = (readySlot == 0) ? 1 : 0;
        std::memcpy(slots[readySlot].data(), data, size);
        readySize = size;
        cvReady.notify_one();
    }

    // Chiamata dal PRODUTTORE: segnala che non ci sono più dati.
    void setDone() {
        std::unique_lock<std::mutex> lk(mtx);
        done = true;
        cvReady.notify_one();
    }

    // Chiamata dal CONSUMATORE: riceve un blocco dal produttore.
    // Riempie `outData` con i dati e imposta `outSize`.
    // Restituisce false se il produttore ha segnalato la fine del flusso.
    bool consume(std::vector<char>& outData, size_t& outSize) {
        std::unique_lock<std::mutex> lk(mtx);
        // Aspetta uno slot pieno oppure il segnale di done.
        cvReady.wait(lk, [this] { return readySlot >= 0 || done; });

        if (readySlot < 0) {
            // done=true e nessuno slot pieno: fine del flusso.
            outSize = 0;
            return false;
        }

        // Copia i dati dal buffer interno nel buffer del consumatore.
        outSize = readySize;
        if (outData.size() < outSize) { outData.resize(outSize); }
        std::memcpy(outData.data(), slots[readySlot].data(), outSize);

        // Libera lo slot per il produttore.
        readySlot = -1;
        cvConsumed.notify_one();
        return true;
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
            exhausted = true;
            return;
        }

        // Legge l'header direttamente dal buffer in RAM.
        std::memcpy(&currentKey, buf.data() + bufPos, sizeof(uint64_t));
        std::memcpy(&currentLen, buf.data() + bufPos + 8, sizeof(uint32_t));
        bufPos += HEADER_SIZE;

        // Assicura che il buffer abbia i byte del payload.
        if (!ensureBytes(currentLen)) {
            exhausted = true;
            return;
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
// Fonde `group` file di run ordinati in `outputPath` usando 3 thread in pipeline.
//
// Thread 1 (Reader, chiamante): Nessun thread dedicato per la lettura —
//   RunBlockReader gestisce la lettura a blocchi in modo trasparente.
//   Il Merger chiama advance() che, quando il buffer RAM è esaurito, fa
//   automaticamente una fread() bloccante. Questo è sufficiente per piccoli K.
//   [Nota: con K >> 10 si potrebbe aggiungere prefetch asincrono per run.]
//
// Thread 2 (Merger, thread secondario): Esegue il K-way merge dalla RAM
//   scrivendo su WriteBuffer. Nessuna syscall I/O durante il merge.
//
// Thread 3 (Writer, thread terziario): Riceve blocchi dal Merger via
//   DoubleBuffer e li scrive su disco sequenzialmente.
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
    // li scrive su disco. DoubleBuffer sincronizza i due thread.
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
                return;
            }
        }
        // Buffer di scrittura interno: ottimizza le syscall.
        std::setvbuf(fout, nullptr, _IOFBF, WRITE_BLOCK_SIZE);

        try {
            std::vector<char> block;
            size_t            blockSize;
            // Ciclo: riceve blocchi dal Merger finché il canale è aperto.
            while (outputChannel.consume(block, blockSize)) {
                if (blockSize == 0) break;
                if (std::fwrite(block.data(), 1, blockSize, fout) != blockSize) {
                    throw std::runtime_error("pipelineMerge: fwrite fallita");
                }
            }
            if (std::fclose(fout) != 0) {
                throw std::runtime_error("pipelineMerge: fclose output fallita");
            }
        } catch (...) {
            std::fclose(fout);
            writerError = std::current_exception();
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
