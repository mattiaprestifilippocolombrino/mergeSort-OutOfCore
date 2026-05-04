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


/*
Nodo Emitter multi-output della farm. Legge un chunk dal file di input,
crea ChunkData e invia ChunkData ai worker.
Prende come input il file di input,
la directory temporanea per salvare le run,
la dimensione del chunk,
il vettore dei paths delle run createe un flag atomico per segnalare errori.
*/
struct FFEmitter : ff_monode_t<ChunkData> {
    FILE*                     fin;         // file di input aperto
    const std::string&        tmpDir;     // directory in cui si salvano le run
    size_t                    chunkBytes; // dimensione massima di ogni chunk
    std::vector<std::string>& runPaths;   // vettore dei path delle run create
    std::atomic<bool>&        errorFlag;  // flag condiviso per segnalare errori

    // Costruttore. Inizializza i membri con i parametri passati.
    FFEmitter(FILE*                    f,
              const std::string&       td,
              size_t                   cb,
              std::vector<std::string>& rp,
              std::atomic<bool>&        ef)
        : fin(f), tmpDir(td), chunkBytes(cb), runPaths(rp), errorFlag(ef)
    {
    }

    /*
    Metodo chiamato da FastFlow quando il nodo deve fare lavoro.
    L'input non viene utilizzato (parametro inutilizzato).
    */
    ChunkData* svc(ChunkData* /*unused*/) override {
        int runId = 0;  // Inizializza l'ID delle run a 0.

        // Il producer continua a leggere chunk finché non arriva alla fine del file 
        // oppure finché non viene segnalato un errore.
        while (true) {
            if (errorFlag.load(std::memory_order_relaxed)) {    // Se un worker ha segnalato errore, interrompo la produzione.
                break;
            }

            ChunkData* chunk  = nullptr;   //chunk contiene il blocco di dati letto dal file.
            bool eofReached  = false;      //eofReached indica se siamo arrivati alla fine del file.

            try {
                // Si prepara un nuovo chunk da inviare alla farm.
                chunk              = new ChunkData();
                chunk->buffer      = new char[chunkBytes];  //Il buffer contiene fisicamente i record letti dal file. Contiene record completi non spezzati.
                chunk->errorFlag  = &errorFlag;   //Viene passato il puntatore al flag atomico per la gestione degli errori.
                size_t bufUsed    = 0;   // Indica quanti byte del buffer sono stati utilizzati per memorizzare i record.

                size_t estimatedRecords = chunkBytes / (HEADER_SIZE + 64);  //Per allocare memoria al vettore di record index in anticipo: stima quanti record ci staranno nel buffer.
                chunk->index.reserve(estimatedRecords);    //Fa una stima del numero di record che entreranno nel chunk e riserva spazio nel vettore index.

                // Secondo ciclo che legge i record dal file finche' il chunk non e' pieno.
                while (true) {
                    // Leggo record completi finche' il chunk ha spazio.
                    RecordHeader hdr;
                    bool gotRecord = readHeader(fin, hdr);  //Legge l’header del prossimo record dal file con l'apposita funzione.

                    if (!gotRecord) {   //Se non viene letto alcun header, si e' giunti alla fine del file. Si esce dal ciclo.
                        eofReached = true;  //Indica che siamo arrivati alla fine del file.
                        break;
                    }

                    size_t totalRecordSize = HEADER_SIZE + hdr.len;  //Calcola la dimensione totale del record.

                    /*
                    L’header è già stato letto. Però se il record completo non entra nel chunk, 
                    il producer torna indietro di HEADER_SIZE byte, 
                    cioè rimette il file pointer prima dell’header appena letto.
                    Così quel record non viene perso: verrà letto nel chunk successivo.
                    */
                    if (bufUsed + totalRecordSize > chunkBytes) {
                        fseeko(fin, -static_cast<off_t>(HEADER_SIZE), SEEK_CUR); // Torna indietro di HEADER_SIZE byte.
                        break; // Esce dal ciclo.
                    }

                    // Se il record entra, lo copia nel buffer.
                    size_t recOffset = bufUsed;  // Salva l'offset di dove inizia il record dentro il buffer.
                    std::memcpy(chunk->buffer + bufUsed,     &hdr.key, sizeof(uint64_t)); // Copia la chiave del record nel buffer.
                    std::memcpy(chunk->buffer + bufUsed + 8, &hdr.len, sizeof(uint32_t)); // Copia la lunghezza del record nel buffer.
                    bufUsed += HEADER_SIZE; // Incrementa buf_used di HEADER_SIZE.


                    // Si legge il payload del record. Si legge len byte dal file. Si usa fread_unlocked per non usare lock interni.
                    size_t bytesRead = fread_unlocked(chunk->buffer + bufUsed, 1, hdr.len, fin);
                    if (bytesRead != hdr.len) {  //Se non sono stati letti len byte si segnala errore, si libera il chunk e si termina lo stream FastFlow.
                        errorFlag.store(true, std::memory_order_relaxed);
                        freeChunk(chunk);
                        return EOS; // segnalo fine al framework FastFlow
                    }
                    bufUsed += hdr.len;  //si aggiorna bufUsed di hdr.len.

                    // Si crea l'indice leggero RecordIndex per il record appena copiato nel buffer, in modo da ordinare senza spostare il payload.
                    RecordIndex ri;
                    ri.key    = hdr.key;  //si memorizza la chiave del record.
                    ri.offset = recOffset;  //si memorizza l'offset del record nel buffer.
                    ri.len    = hdr.len;  //si memorizza la lunghezza del record.
                    chunk->index.push_back(ri);  //si aggiunge l'indice al vettore degli indici.
                }

                // Se non e' stato letto alcun record, si libera la memoria allocata per il chunk e si esce dal ciclo.
                if (chunk->index.empty()) {
                    freeChunk(chunk);
                    break;
                }

                //Si genera il path del file di run e lo si aggiunge al vettore delle run.
                chunk->runPath = tmpDir + "/run_" + std::to_string(runId) + ".bin";
                runPaths.push_back(chunk->runPath);
                ++runId;  //Si incrementa l'ID delle run.

                // Si invia il chunk ad un worker.
                ff_send_out(chunk);

            } catch (...) {  //In caso di errore, si libera la memoria allocata per il chunk e si esce dal ciclo.
                errorFlag.store(true, std::memory_order_relaxed);
                freeChunk(chunk);
                break;
            }

            if (eofReached) { //Se e' stata raggiunta la fine del file si esce dal ciclo.
                break;
            }
        }

        // Se la fase di lettura del file e' terminata in modo corretto, si segnala ai worker che non arriveranno altri chunk inviando EOS.
        return EOS;
    }
};


/*
Il producer invia un chunk e poi dimentica di averlo mandato. Non sa quando tornerà indietro.
Il worker riceve il chunk e lavora in background.
Quando il producer finisce, invia EOS. Smette di inviare chunk e aspetta che i worker finiscano il lavoro residuo.
*/


/*
Il worker riceve un puntatore a un ChunkData. Il worker lo ordina e lo salva in un file di run. 
Il valore restituito dal worker è il segnale che il worker vuole continuare a ricevere task.
Il worker non contiene logica nuova: riusa la stessa funzione usata da OpenMP. Cambia solo il pattern parallelo.
Non si ha collector..
*/
struct FFWorker : ff_node_t<ChunkData> {
    ChunkData* svc(ChunkData* chunk) override {  //funzione che viene eseguita dal worker.
        // Chiama la funzione sortChunkAndWriteRun per ordinare il chunk e scrivere la run su disco.
        sortChunkAndWriteRun(chunk);

        // GO_ON dice a FastFlow:"ho finito questo task, sono pronto per il prossimo".
        // Non c'e' un collector, quindi non inviamo dati in uscita.
        return GO_ON;  //Il worker comunica a ff che ha finito il task e può riceverne un altro.
    }
};

// Versione FastFlow della generazione delle run. Utilizza le strutture create precedentemente.
// Prende in input il path del file di input, la directory temporanea che contiene le run,
// la dimensione del buffer e il numero di worker. Ritorna un vettore contenente i path delle run generate.
inline std::vector<std::string> ffSortToRuns(
    const std::string& inputPath,  //path del file di input.
    const std::string& tmpDir,     //path della directory temporanea dove salvare le run.
    size_t             chunkBytes, //dimensione del buffer.
    int                nWorkers)   //numero di worker.
{
    validateChunkBytes(chunkBytes);  //controlla che la dimensione di un chunk sia abbastanza grande da contenere almeno un record di dimensione massima.

    FILE* fin = std::fopen(inputPath.c_str(), "rb");  //apertura file di input in lettura binaria.
    if (fin == nullptr) {
        throw std::runtime_error("ff_sort_to_runs: impossibile aprire " + inputPath);  //lancio eccezione se il file non viene aperto.
    }
    std::setvbuf(fin, nullptr, _IOFBF, 4 * 1024 * 1024);  //si imposta un buffer di 4MB per la lettura.

    std::vector<std::string> runPaths;  //vettore che conterra' i path delle run generate.
    runPaths.reserve(64);  //si riserva memoria per 64 run.
    std::atomic<bool> errorFlag{false};  //flag atomico per segnalare errori.


    // Si crea un emitter. Prende in input il path del file di input, la directory temporanea che contiene le run,
    // la dimensione del buffer e il numero di worker. Ritorna un vettore contenente i path delle run generate.
    FFEmitter emitter(fin, tmpDir, chunkBytes, runPaths, errorFlag);

    // FastFlow vuole un vettore di ff_node*. Io tengo gli oggetti veri in
    // w_owned e passo a FastFlow solo i puntatori grezzi.
    std::vector<std::unique_ptr<FFWorker>> w_owned;  //si crea un vettore di worker.
    w_owned.reserve(nWorkers);  //si riserva memoria per nWorkers.

    std::vector<ff_node*> w_ptrs;  //si crea un vettore di puntatori a worker.
    w_ptrs.reserve(nWorkers);  //si riserva memoria per nWorkers.

    for (int i = 0; i < nWorkers; i++) {
        w_owned.push_back(std::make_unique<FFWorker>());  //si crea un worker e lo si aggiunge al vettore degli worker.
        w_ptrs.push_back(w_owned.back().get());  //si aggiunge il puntatore del worker al vettore dei puntatori ai worker.
    }

    // Si costruisce la farm: Emitter -> Worker[0..N-1] (senza Collector).
    ff_farm farm;  //si crea la farm.
    farm.add_emitter(&emitter);  //si aggiunge l'emitter alla farm.
    farm.add_workers(w_ptrs);  //si aggiunge il vettore dei puntatori ai worker alla farm.
    farm.remove_collector();   // si rimuove il collector 

    // Avvio la farm e aspetto che finisca (bloccante).
    int ret = farm.run_and_wait_end();  //esegue la farm bloccando fin quando tutti i task non sono finiti.
    if (ret < 0) {  //controllo se la farm e' terminata con errore.
        throw std::runtime_error("ff_sort_to_runs: farm FastFlow terminata con errore");  //lancio eccezione se la farm e' terminata con errore.
    }

    std::fclose(fin);  //chiusura file di input.

    if (errorFlag.load()) {  //controllo se c'e' stato un errore in qualche worker. Se si, viene lanciata un'eccezione.
        throw std::runtime_error("ff_sort_to_runs: errore in un worker FastFlow");  //lancio eccezione se c'e' stato un errore in qualche worker.
    }

    return runPaths;  //ritorno del vettore contenente i path delle run generate.
}
