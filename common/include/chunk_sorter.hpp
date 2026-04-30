#pragma once

/*
Fase 1 del MergeSort out-of-core. 
Trasforma il file di input in una sequenza di run ordinate.
Una run e' un file temporaneo contenente un sottoinsieme di record, gia' ordinato per chiave.

Durante std::sort NON si spostano i payload, perche' possono essere grandi.
Si costruisce invece un vettore di RecordIndex: 
RecordIndex = { key, offset nel buffer, len }
In questo modo std::sort sposta elementi piccoli. Il payload resta
fermo nel buffer e viene copiato solo quando scrivo la run ordinata.

Schema:
   input grande
        |
        |
        v
   chunk_0, chunk_1, chunk_2, ...
        |
        | ogni chunk viene ordinato da un task OpenMP
        v
   run_0.bin, run_1.bin, run_2.bin, ...
Parallelismo: Un solo thread legge il file in modo sequenziale, mentre i worker
ordinano e scrivono i chunk gia' letti. 
*/
#include "record.hpp"

#include <algorithm>   // std::sort
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <cstdint>
#include <omp.h>

/* Il record vero sul disco ha forma: [key: 8 byte][len: 4 byte][payload: len byte]
Nel vettore da ordinare non salviamo l'intero payload, perche' puo' essere grande,
ma solo:
- key:    serve per il confronto;
- offset: posizione del record nel buffer;
- len:    lunghezza del payload.
Il payload non viene quindi mai spostato durante std::sort.
*/
struct RecordIndex {
    uint64_t key;     // chiave di ordinamento
    size_t   offset;  // byte offset del record all'interno del buffer del chunk
    uint32_t len;     // lunghezza del payload

    // Operatore < usato da std::sort: Confronta la key di due record.
    bool operator<(const RecordIndex& other) const noexcept {
        return key < other.key;
    }
};

/*
Dati di input di un task OpenMP, relativi ad un chunk da processare.
Il thread lettore alloca ChunkData, lo riempie, crea un task e poi non lo
tocca piu'. Da quel momento il task e' responsabile di ordinare index.
Si ha un buffer contenente tutti i record del chunk di input, un vettore di record index
e il path del file di run da creare.
L'ownership e' comunque chiara: dopo la creazione del task, il worker deve fare delete.
*/
struct ChunkData {
    char*                    buffer;    // buffer grezzo con tutti i record del chunk
    std::vector<RecordIndex> index;     // Vettore di RecordIndex contenente la chiave, l'offset e la lunghezza di ogni record.
    std::string              run_path;  // path del file di run da creare
    std::atomic<bool>*       error_flag; // flag condiviso per segnalare errori
};


// Funzione inline che libera la memoria allocata per un ChunkData. Dealloca sia il buffer che la struct.
inline void free_chunk(ChunkData* chunk) {
    if (chunk == nullptr) {
        return;
    }
    delete[] chunk->buffer;     // Libera il buffer grezzo contenente tutti i record del chunk.
    delete chunk;               // Libera la struct ChunkData.
}

/*
Funzione che serve a controllare che la dimensione di un chunk 
sia abbastanza grande da contenere almeno un record di dimensione massima nel caso peggiore.
*/
inline void validate_chunk_bytes(size_t chunk_bytes) {
    const size_t min_record = static_cast<size_t>(HEADER_SIZE) + PAYLOAD_MAX;
    if (chunk_bytes < min_record) {
        throw std::runtime_error(
            "chunk troppo piccolo: deve contenere almeno un record massimo (" +
            std::to_string(min_record) + " byte)"
        );
    }
}

/*
Funzione eseguita da un thread worker.
Ordina il vettore di RecordIndex per key. Apre il file run_N.bin.
Scorre l'indice ordinato e scrive i record nel nuovo ordine.
Libera il buffer del chunk. Per la gestione degli errori si setta un flag atomico letto poi dal thread principale.
*/
inline void sort_chunk_and_write_run(ChunkData* chunk) {
    try {
        // Si ordina il vettore di RecordIndex per key, usando std::sort e l'operatore < implementato in RecordIndex.
        std::sort(chunk->index.begin(), chunk->index.end());

        // Ogni chunk produce una run temporanea indipendente. Si apre il file run_N.bin in scrittura binaria.
        FILE* fout = std::fopen(chunk->run_path.c_str(), "wb");

        // Controllo che il file sia stato aperto correttamente. In caso negativo, viene stampato un messaggio di errore e liberata la memoria del chunk.
        if (fout == nullptr) {
            std::fprintf(stderr, "[ERRORE] Impossibile creare %s\n",
                         chunk->run_path.c_str());
            chunk->error_flag->store(true, std::memory_order_relaxed);
            free_chunk(chunk);
            return;
        }

        // Si imposta un buffer di 4MB per la scrittura: Ogni 4 MB viene effettuata una fwrite.
        std::setvbuf(fout, nullptr, _IOFBF, 4 * 1024 * 1024);

        bool write_ok = true;
        //Si scorre il vettore di RecordIndex ordinato per key e si scrivono i record nel nuovo ordine.
        for (const RecordIndex& ri : chunk->index) {
            // Si inizia a scrivere dal punto in cui si trova il record nel buffer:
            // Puntatore all'inizio del record nel buffer del chunk + offset del record rispetto all'inizio del chunk.
            const char* rec = chunk->buffer + ri.offset;

// Nel buffer il record e'contiguo: [header][payload], quindi si scrive con una sola fwrite di dimensione
// HEADER_SIZE + len. Si usa fwrite_unlocked per rimuovere l'overhead dei lock interni alla libc, essendo la scrittura svolta da un solo thread.
            size_t record_size    = HEADER_SIZE + ri.len;
            size_t bytes_written  = fwrite_unlocked(rec, 1, record_size, fout);

            // Controllo che la scrittura sia andata a buon fine.
            if (bytes_written != record_size) {
                std::fprintf(stderr, "[ERRORE] fwrite fallita su %s\n",
                             chunk->run_path.c_str());
                write_ok = false;
                break;
            }
        }

        // Chiusura file.
        if (std::fclose(fout) != 0) {
            write_ok = false;
        }

        // In caso di errore di scrittura, viene settato il flag atomico per segnalare l'errore al thread principale.
        if (!write_ok) {
            chunk->error_flag->store(true, std::memory_order_relaxed);
        }

    } catch (...) {
        chunk->error_flag->store(true, std::memory_order_relaxed);
    }

    // Libera il chunk finita la scrittura della run.
    free_chunk(chunk);
}


/*
Funzione che rappresenta la fase 1 del merge sort out-of-core.
Apre il file input. Si posiziona a start_offset. Legge record completi fino a riempire un chunk.
Salva nel chunk il buffer e l'indice di RecordIndex. Crea un task OpenMP.
Il task ordina il chunk e scrive una run temporanea. Ripete fino a EOF o end_offset.
Aspetta che tutti i task terminano. Restituisce i path delle run.
Ordina solo l'intervallo [start_offset, end_offset) del file. Per la versione single-node si usa start=0 e end=-1, 
cioe' si legge fino alla fine del file. Nella versione multi-node, ogni rank riceve una porzione diversa del file.
max_inflight limita quanti chunk possono essere stati letti ma non ancora completati dai worker. 
Questo evita di consumare troppa RAM se il lettore e' piu' veloce dei task di sort/scrittura.

Si ha come input il path del file di input, il path della directory temporanea, la dimensione dei chunk in byte, 
l'offset di inizio lettura, l'offset di fine lettura e il limite di chunk in volo. 
Restituisce i path delle run generate.
*/
inline std::vector<std::string> sort_range_to_runs(
    const std::string& input_path, // Path del file di input.
    const std::string& tmp_dir,    // Path della directory temporanea.
    size_t             chunk_bytes,    // Dimensione dei chunk in byte.
    int64_t            start_offset, // Offset di inizio lettura. Viene usato nella versiono MPI per indicare il punto di partenza della porzione di file assegnata a ciascun rank.
    int64_t            end_offset,   // Offset di fine lettura. Viene usato nella versione MPI per indicare il punto di arrivo della porzione di file assegnata a ciascun rank.
    int                max_inflight = 0) // Controlla quanti task OpenMP possono essere “in volo”, cioè allocati e non ancora completati.
{
    // Controllo che il chunk sia abbastanza grande da contenere almeno un record.
    validate_chunk_bytes(chunk_bytes);

    // Apre il file di input in lettura binaria.
    FILE* fin = std::fopen(input_path.c_str(), "rb");
    if (fin == nullptr) {
        throw std::runtime_error("sort_to_runs: impossibile aprire " + input_path);
    }

    // Usato solo nella versione MPI. Sposta il puntatore di lettura all'offset di inizio. 
    // Per la versione single-node si usa start=0 e end=-1, cioè si legge fino alla fine del file.
    // Nella versione MPI ogni rank riceve una porzione diversa del file.
    if (fseeko(fin, static_cast<off_t>(start_offset), SEEK_SET) != 0) {
        throw std::runtime_error("sort_to_runs: seek iniziale fallita");
    }

    // Si utilizza un buffer di lettura grande (4MB) per ridurre il numero di syscall.
    std::setvbuf(fin, nullptr, _IOFBF, 4 * 1024 * 1024);

    // Vettori che conterranno i path delle run generate.
    std::vector<std::string> run_paths;
    run_paths.reserve(64);

    // Variabile booleana atomica che viene settata a true se un task ha errori.
    std::atomic<bool> error_flag{false};

    /*
    Si decide quanti chunk possono essere elaborati contemporaneamente, ovvero quanti task si lanciano.
    Se max_inflight viene passato esplicitamente, si usa quello.
    Altrimenti usa: 2 * numero_thread_OpenMP
    */
    int task_window;
    if (max_inflight > 0) {
        task_window = max_inflight;
    } else {
        task_window = std::max(2, 2 * omp_get_max_threads());
    }

/*
Un solo thread legge il file e crea i chunk, mentre gli altri thread eseguono i task di ordinamento e scrittura.
Le variabili condivise sono: fin, tmp_dir, chunk_bytes, run_paths, error_flag, end_offset, task_window.
*/    
    #pragma omp parallel default(none) shared(fin, tmp_dir, chunk_bytes, run_paths, error_flag, end_offset, task_window)
    #pragma omp single
    {
        int run_id    = 0; // Id progressivo delle run generate.
        int in_flight = 0;  // Numero di task in volo attuali.

        // Ciclo finche' non si raggiunge la fine del file, della parte di file assegnata ( MPI ) o un errore.
        while (true) {
            // Se un worker ha fallito, smettiamo di produrre altro lavoro.
            if (error_flag.load(std::memory_order_relaxed)) {
                break;
            }

            // Nella versione MPI se si supera il limite di chunk da leggere, si smette.
            // Si legge l'offset corrente e si confronta con l'offset di fine. Se e' maggiore, si smette.
            if (end_offset >= 0) {
                off_t pos = ftello(fin);
                if (pos < 0 || pos >= static_cast<off_t>(end_offset)) {
                    break;
                }
            }

            ChunkData* chunk     = nullptr;     // Chunk da ordinare.
            bool eof_reached     = false;       // Indica se e' stata raggiunta la fine del file.

            try {
                // Per ogni iterazione viene creato un nuovo chunk. 
                chunk              = new ChunkData(); // Alloco un buffer grande quanto il chunk richiesto. 
                chunk->buffer      = new char[chunk_bytes]; // Il buffer contiene record completi, mai record spezzati.
                chunk->error_flag  = &error_flag; // Flag condiviso di errore-
                size_t buf_used    = 0; 

                // La funzione prova a stimare quanti record entreranno nel chunk, assumendo payload medio di almeno 64 byte.
                // Serve solo a ridurre riallocazioni del vettore.
                size_t estimated_records = chunk_bytes / (HEADER_SIZE + 64);
                chunk->index.reserve(estimated_records);    // Alloca memoria sufficiente per contenere il numero stimato di record.

                //Dentro il secondo while la funzione legge record completi finché il chunk non è pieno.
                while (true) {
                    if (error_flag.load(std::memory_order_relaxed)) {
                        eof_reached = true;
                        break;
                    }

                    // Nella versione MPI controllo il limite di stripe.
                    if (end_offset >= 0) {
                        off_t pos = ftello(fin);
                        if (pos < 0 || pos >= static_cast<off_t>(end_offset)) {
                            eof_reached = true;
                            break;
                        }
                    }

                    // Legge il record header in una struct temporanea. 
                    // Se fallisce, significa che si è arrivati alla fine del file (EOF). 
                    // La funzione read_header gestisce internamente il caso di file troncato (record parziale).
                    RecordHeader hdr;
                    bool got_record = read_header(fin, hdr);  // Legge l'header con l'apposita funzione. 
                    if (!got_record) {
                        eof_reached = true;
                        break; // EOF
                    }

                    // Calcola la dimensione totale del record (header + payload). 
                    size_t total_record_size = HEADER_SIZE + hdr.len;

                    // Se siamo in MPI, controllo che il record letto cada
                    // interamente nella stripe del rank.
                    // Per i rank MPI è necessario assicurarsi che un record non venga tagliato a metà tra due stripe.
                    //Fa un passo indietro di HEADER_SIZE, cioè torna prima dell’header, e termina.
                    // In questo modo evita di produrre un record spezzato. 
                    if (end_offset >= 0) {
                        off_t after_header = ftello(fin);
                        int64_t rec_start  = static_cast<int64_t>(after_header) - HEADER_SIZE;

                        if (rec_start + static_cast<int64_t>(total_record_size) > end_offset) {
                            // Il record sconfina oltre la stripe: torno indietro.
                            fseeko(fin, -static_cast<off_t>(HEADER_SIZE), SEEK_CUR);
                            eof_reached = true;
                            break;
                        }
                    }

/*
Il chunk corrente potrebbe essere quasi pieno. 
Se il record successivo non ci sta, non posso consumarlo qui e non posso neanche buttarlo via.
Allora torno indietro di HEADER_SIZE (cioè mi riposiziono all’inizio dell’header del record) e termino il ciclo.
L’iterazione successiva del while userà un nuovo buffer di chunk_bytes e rileggerà quel record per primo.
*/
                    if (buf_used + total_record_size > chunk_bytes) {
                        fseeko(fin, -static_cast<off_t>(HEADER_SIZE), SEEK_CUR);
                        break;
                    }

                    // Salva dove inizia il record dentro il buffer.
                    size_t rec_offset = buf_used;

                    // Copio l'header nel buffer del chunk. Aumento buf_used di HEADER_SIZE.
                    std::memcpy(chunk->buffer + buf_used,     &hdr.key, sizeof(uint64_t));
                    std::memcpy(chunk->buffer + buf_used + 8, &hdr.len, sizeof(uint32_t));
                    buf_used += HEADER_SIZE;

                    // Leggo il payload e lo inserisco nel buffer del chunk. 
                    // Si usa anche qui fread senza lock, per velocizzare la lettura seriale del file evitando contention.
                    size_t bytes_read = fread_unlocked(chunk->buffer + buf_used, 1, hdr.len, fin);

                    // Controllo se il payload e' stato letto tutto. Altrimenti, lancia un'eccezione.
                    if (bytes_read != hdr.len) {
                        throw std::runtime_error("sort_to_runs: payload troncato");
                    }

                    // Aumento buf_used di hdr.len.
                    buf_used += hdr.len;

                    // Salvo in RecordIndex tutti i dati relativi al record, compreso l'offset da dove inizia il record nel buffer del chunk.
                    // Si inserisce il RecordIndex nell'indice del chunk.
                    RecordIndex ri;
                    ri.key    = hdr.key;
                    ri.offset = rec_offset;
                    ri.len    = hdr.len;
                    chunk->index.push_back(ri);
                }

                // Se non ho letto nessun record, libero la memoria del chunk e passo al successivo. 
                // Questo puo' succedere se si arriva alla fine del file (EOF).
                if (chunk->index.empty()) {
                    free_chunk(chunk);
                    break;
                }

                // Assegno il nome del file run e lo aggiungo al vector run_paths.
                chunk->run_path = tmp_dir + "/run_" + std::to_string(run_id) + ".bin";
                run_paths.push_back(chunk->run_path);
                ++run_id;

                // PARTE PARALLELA: Si lancia un task per ordinare il chunk. Il task ha come dato privato le info del chunk.
                // Si usa la funzione implementata precedentemente per ordinare in parallelo il chunk e scriverlo sul disco. 
                #pragma omp task firstprivate(chunk) default(none)
                {
                    sort_chunk_and_write_run(chunk);
                }
                ++in_flight;   //Si incrementa il contatore dei task lanciati.

                /*
                Senza questo limite, il thread principale potrebbe leggere il file troppo velocemente e creare
                tantissimi chunk in RAM.
                task_window limita il numero massimo di task che possono essere in esecuzione contemporaneamente.
                Una volta che il numero di task in esecuzione raggiunge task_window, il thread principale attende 
                che tutti i task in esecuzione siano completati prima di procedere.
                */
                if (in_flight >= task_window) {
                    #pragma omp taskwait   // Attende che tutti i task in esecuzione siano completati.
                    in_flight = 0;  // Resetta il contatore dei task in esecuzione.
                }
            //Se c’è stato un errore, si esce dal while.
            } catch (...) {
                error_flag.store(true, std::memory_order_relaxed);
                free_chunk(chunk);
                break;
            }
            // Se si è arrivati alla fine del file, si esce dal while.
            if (eof_reached) {
                break;
            }
        }

        // Si aspetta che tutti i task lanciati siano completati. Si chiude poi il file. 
        #pragma omp taskwait
        std::fclose(fin);
    }

    if (error_flag.load()) {
        throw std::runtime_error("sort_to_runs: errore in uno o più task OMP");
    }

    return run_paths;
}

// Wrapper usato dalle versioni single-node: ordina tutto il file. 
//Si passa 0 come start e -1 come end per indicare "leggi dal principio fino alla fine".
inline std::vector<std::string> sort_to_runs(
    const std::string& input_path,
    const std::string& tmp_dir,
    size_t             chunk_bytes)
{
    return sort_range_to_runs(input_path, tmp_dir, chunk_bytes, 0, -1);
}
