#pragma once
// =============================================================================
// ff_chunk_sorter.hpp - Fase 1 con FastFlow
// =============================================================================
//
// Questa e' la stessa fase logica di chunk_sorter.hpp, ma implementata con una
// farm FastFlow invece che con task OpenMP.
//
// Architettura:
//
//   Emitter
//      legge chunk dal file di input
//      crea ChunkData
//      invia ChunkData ai worker
//
//   Worker 0..W-1
//      riceve un ChunkData
//      chiama sort_chunk_and_write_run
//      scrive una run ordinata su disco
//
// Non uso un collector perche' non ho bisogno di raccogliere risultati in RAM:
// ogni worker produce direttamente un file temporaneo. Questo mantiene semplice
// il codice e riduce passaggi inutili di dati nel framework.
//
// Anche qui resta valida l'idea piu' importante: sort su indice leggero, non sui
// payload. La funzione sort_chunk_and_write_run e' condivisa con OpenMP.
// =============================================================================

#include "record.hpp"
#include "chunk_sorter.hpp"   // ChunkData, sort_chunk_and_write_run

#include <ff/ff.hpp>
#include <ff/farm.hpp>

#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <memory>

using namespace ff;

// Emitter della farm.
//
// In FastFlow l'emitter e' il produttore dei task. Qui fa quasi lo stesso lavoro
// del thread single nella versione OpenMP: legge un chunk, costruisce l'indice e
// invia il puntatore al primo worker disponibile.
//
// ff_monode_t<ChunkData> significa:
//   - questo nodo e' un "multi-output node" (produce output verso piu' worker)
//   - il tipo di dato che invia e' ChunkData*
struct FFEmitter : ff_monode_t<ChunkData> {
    FILE*                     fin;         // file di input aperto
    const std::string&        tmpDir;     // directory per le run
    size_t                    chunkBytes; // dimensione massima di ogni chunk
    std::vector<std::string>& runPaths;   // accumulatore dei path delle run create
    std::atomic<bool>&        errorFlag;  // flag condiviso per segnalare errori

    FFEmitter(FILE*                    f,
              const std::string&       td,
              size_t                   cb,
              std::vector<std::string>& rp,
              std::atomic<bool>&        ef)
        : fin(f), tmpDir(td), chunkBytes(cb), runPaths(rp), errorFlag(ef)
    {
    }

    // svc() e' il metodo chiamato da FastFlow quando il nodo deve fare lavoro.
    // Parametro: non usato per l'emitter (non ha input), restituisce EOS per segnalare
    // la fine della produzione.
    ChunkData* svc(ChunkData* /*unused*/) override {
        int runId = 0;

        while (true) {
            // Se un worker ha segnalato errore, interrompo la produzione.
            if (errorFlag.load(std::memory_order_relaxed)) {
                break;
            }

            ChunkData* chunk  = nullptr;
            bool eofReached  = false;

            try {
                // Preparo un nuovo chunk da inviare alla farm.
                chunk              = new ChunkData();
                chunk->buffer      = new char[chunkBytes];
                chunk->errorFlag  = &errorFlag;
                size_t bufUsed    = 0;

                size_t estimatedRecords = chunkBytes / (HEADER_SIZE + 64);
                chunk->index.reserve(estimatedRecords);

                while (true) {
                    // Leggo record completi finche' il chunk ha spazio.
                    RecordHeader hdr;
                    bool gotRecord = readHeader(fin, hdr);

                    if (!gotRecord) {
                        eofReached = true;
                        break;
                    }

                    size_t totalRecordSize = HEADER_SIZE + hdr.len;

                    // Il record non entra nel chunk corrente: torno indietro.
                    if (bufUsed + totalRecordSize > chunkBytes) {
                        fseeko(fin, -static_cast<off_t>(HEADER_SIZE), SEEK_CUR);
                        break;
                    }

                    // Copio header e payload nel buffer del chunk.
                    size_t recOffset = bufUsed;
                    std::memcpy(chunk->buffer + bufUsed,     &hdr.key, sizeof(uint64_t));
                    std::memcpy(chunk->buffer + bufUsed + 8, &hdr.len, sizeof(uint32_t));
                    bufUsed += HEADER_SIZE;

                    // [OTTIMIZZAZIONE] Uso fread_unlocked per coerenza e velocizzare I/O.
                    size_t bytesRead = fread_unlocked(chunk->buffer + bufUsed, 1, hdr.len, fin);
                    if (bytesRead != hdr.len) {
                        errorFlag.store(true, std::memory_order_relaxed);
                        freeChunk(chunk);
                        return EOS; // segnalo fine al framework FastFlow
                    }
                    bufUsed += hdr.len;

                    // Salvo il descrittore leggero da ordinare.
                    RecordIndex ri;
                    ri.key    = hdr.key;
                    ri.offset = recOffset;
                    ri.len    = hdr.len;
                    chunk->index.push_back(ri);
                }

                // Nessun record letto: EOF.
                if (chunk->index.empty()) {
                    freeChunk(chunk);
                    break;
                }

                chunk->runPath = tmpDir + "/run_" + std::to_string(runId) + ".bin";
                runPaths.push_back(chunk->runPath);
                ++runId;

                // Invia il puntatore a un worker. Da qui il worker possiede chunk.
                ff_send_out(chunk);

            } catch (...) {
                errorFlag.store(true, std::memory_order_relaxed);
                freeChunk(chunk);
                break;
            }

            if (eofReached) {
                break;
            }
        }

        // EOS (End Of Stream) segnala ai worker che non arriveranno altri chunk.
        return EOS;
    }
};

// Worker della farm.
//
// Il worker non contiene logica nuova: riusa la stessa funzione usata da OpenMP.
// Questo e' utile per l'esame perche' le due versioni single-node differiscono
// nel pattern parallelo, non nell'algoritmo di ordinamento.
//
// ff_node_t<ChunkData> significa:
//   - questo nodo riceve e invia messaggi di tipo ChunkData*
struct FFWorker : ff_node_t<ChunkData> {
    ChunkData* svc(ChunkData* chunk) override {
        // Ordina il chunk e scrive la run su disco.
        // La funzione dealloca chunk internamente quando ha finito.
        sortChunkAndWriteRun(chunk);

        // GO_ON dice a FastFlow: "ho finito questo task, sono pronto per il prossimo".
        // Non c'e' un collector, quindi non inviamo dati in uscita.
        return GO_ON;
    }
};

// Versione FastFlow della generazione delle run.
// Ritorna i path delle run ordinate esattamente come sort_to_runs.
inline std::vector<std::string> ffSortToRuns(
    const std::string& inputPath,
    const std::string& tmpDir,
    size_t             chunkBytes,
    int                nWorkers)
{
    validateChunkBytes(chunkBytes);

    FILE* fin = std::fopen(inputPath.c_str(), "rb");
    if (fin == nullptr) {
        throw std::runtime_error("ff_sort_to_runs: impossibile aprire " + inputPath);
    }
    std::setvbuf(fin, nullptr, _IOFBF, 4 * 1024 * 1024);

    std::vector<std::string> runPaths;
    runPaths.reserve(64);
    std::atomic<bool> errorFlag{false};

    // L'emitter vive sullo stack: la farm usa il puntatore ma non ne prende
    // ownership. I worker invece sono tenuti in unique_ptr per liberarli
    // automaticamente a fine funzione.
    FFEmitter emitter(fin, tmpDir, chunkBytes, runPaths, errorFlag);

    // FastFlow vuole un vettore di ff_node*. Io tengo gli oggetti veri in
    // w_owned e passo a FastFlow solo i puntatori grezzi.
    std::vector<std::unique_ptr<FFWorker>> w_owned;
    w_owned.reserve(nWorkers);

    std::vector<ff_node*> w_ptrs;
    w_ptrs.reserve(nWorkers);

    for (int i = 0; i < nWorkers; i++) {
        w_owned.push_back(std::make_unique<FFWorker>());
        w_ptrs.push_back(w_owned.back().get());
    }

    // Costruisco la farm: Emitter -> Worker[0..N-1] (senza Collector).
    ff_farm farm;
    farm.add_emitter(&emitter);
    farm.add_workers(w_ptrs);
    farm.remove_collector();   // i worker scrivono file, non messaggi

    // Avvio la farm e aspetto che finisca (bloccante).
    int ret = farm.run_and_wait_end();
    if (ret < 0) {
        throw std::runtime_error("ff_sort_to_runs: farm FastFlow terminata con errore");
    }

    std::fclose(fin);

    if (errorFlag.load()) {
        throw std::runtime_error("ff_sort_to_runs: errore in un worker FastFlow");
    }

    return runPaths;
}
