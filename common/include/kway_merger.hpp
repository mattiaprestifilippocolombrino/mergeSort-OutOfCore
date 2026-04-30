#pragma once
// =============================================================================
// kway_merger.hpp - Fase 2 del MergeSort out-of-core
// =============================================================================
/*
Fase 2 del mergeSort out-of-core.
Modulo che effettua la fusione (merge) di più file ordinati (run) in un unico file ordinato.
Algoritmo del merge K-way:
Si aprono K run ordinate. Si legge il primo record di ogni run.
Si mette in una min-heap la key del record corrente di ogni run. 
Si estrae sempre la key minima. Si scrive quel record in output. 
Si avanza solo la run da cui ho preso il record. Si ripete finche' la heap e' vuota.


Se le run totali sono piu' di merge_fan, non le apro tutte insieme, ma faccio merge multi-pass. 
Scelta prestazionale: uso merge_fan = 64 di default. Con buffer da 4 MB per run sono circa
256 MB, meno dei 32 GB per nodo, ma riduco il numero di passate su disco.
*/


#include "record.hpp"
#include <vector>
#include <queue>
#include <string>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <atomic>

// Buffer di 4 MB per ogni run aperta in merge. 
//Il merge fa molte letture sequenziali piccole; il buffer riduce le syscall.
static constexpr size_t MERGE_BUF_SIZE = 4ULL * 1024 * 1024;

/*
Struttura wrapper RunReader usata per leggere una run ordinata.
Tiene sempre caricato il "record corrente" della run: current_key, current_len, payload, oltre al file aperto.
Quando un record viene scritto in output, chiamo advance() solo su quella run.
*/
struct RunReader {
    FILE*             file;         // puntatore al file aperto della run
    uint64_t          currentKey;  // key del record corrente
    uint32_t          currentLen;  // lunghezza payload corrente
    std::vector<char> payload;      // buffer payload corrente
    bool              exhausted;    // true se la run e' finita

    // Costruttore: apre il file e precarica il primo record. Inizializza tutti i campi e controlla se il file è stato aperto correttamente.
    explicit RunReader(const std::string& path) {
        file         = std::fopen(path.c_str(), "rb");
        currentKey  = 0;
        currentLen  = 0;
        exhausted    = false;

        if (file == nullptr) {
            throw std::runtime_error("RunReader: impossibile aprire " + path);
        }

        // Setta il buffer per la lettura. In questo modo si riducono le syscall.
        std::setvbuf(file, nullptr, _IOFBF, MERGE_BUF_SIZE);

        // Precarico subito il primo record della run.
        advance();
    }

    // Il distruttore chiude il file
    ~RunReader() {
        if (file != nullptr) {
            std::fclose(file);
        }
    }

    // Blocco le operazioni di copia e assegnazione
    RunReader(const RunReader&)            = delete;
    RunReader& operator=(const RunReader&) = delete;

    // Funzione che avanza alla run successiva. Legge sempre il primo record da processare di ogni run.
    void advance() {
        RecordHeader hdr;
        // Legge l'header del record successivo
        bool gotRecord = readHeader(file, hdr);

        // Se non ci sono più record, imposta exhausted a true e ritorna
        if (!gotRecord) {
            exhausted = true;
            return;
        }

        // Aggiorna la key e la lunghezza del record corrente. Ridimensiona il buffer del payload.
        currentKey = hdr.key;
        currentLen = hdr.len;
        payload.resize(hdr.len);

        // Legge il payload del record corrente dal file. Usa anche qui fread_unlocked.
        size_t bytesRead = fread_unlocked(payload.data(), 1, hdr.len, file);
        if (bytesRead != hdr.len) {
            throw std::runtime_error("RunReader::advance: payload troncato");
        }
    }
};


/*
Struttura che definisce una entry dell'heap. Contiene la key e l'indice della run da cui proviene.
Anche qui non si mette il payload per motivi di performance.
*/
struct HeapEntry {
    uint64_t key;
    int      runIdx;

    // Operatore > usato dalla priority_queue per ottenere una min-heap.
    // std::priority_queue e' max-heap di default. Passando std::greater<HeapEntry>
    // come Comparator, l'elemento con key piu' piccola sta sempre in cima.
    bool operator>(const HeapEntry& other) const noexcept {
        return key > other.key;
    }
};


// Funzione usata nei casi banali con una sola run. 
// Prende in input src e dst e rinomina src in dst se delete_src è true, altrimenti copia src in dst
inline void moveOrCopyRun(const std::string& src,
                             const std::string& dst,
                             bool deleteSource)
{
    // Nessuna operazione necessaria se src e dst coincidono.
    if (src == dst) {
        return;
    }

    if (deleteSource) {
        // Si rinomina il file sorgente in dst. E' atomico e non copia i dati: O(1).
        if (std::rename(src.c_str(), dst.c_str()) != 0) {
            throw std::runtime_error("move_or_copy_run: rename fallito");
        }
    } else {
        // Si copia il file mantenendo il sorgente (modalita' keep-runs).
        std::error_code ec;
        std::filesystem::copy_file(
            src, dst,
            std::filesystem::copy_options::overwrite_existing,
            ec
        );
        if (ec) {
            throw std::runtime_error("move_or_copy_run: copia fallita: " + ec.message());
        }
    }
}


/*
Funzione che fonde un gruppo di K run in un solo file ordinato.
Prende come input un vettore di path di run da fondere, il path del file di output e 
un booleano che indica se cancellare le run sorgente.
Costo CPU: O(N log K), dove K e' il numero di run nel gruppo.
Costo I/O: legge tutti i record del gruppo e li riscrive una volta.
*/
inline void mergePass(
    const std::vector<std::string>& group,      // Vettore di path delle run da fondere
    const std::string&              output,     // Path del file di output
    bool                            deleteSource) // Indica se cancellare le run sorgente
{
    // Numero K di run nel gruppo.
    const int K = static_cast<int>(group.size());

    // Caso banale: Se K è 1, si ha una sola run già ordinata, quindi non c'è nulla da fondere.
    if (K == 1) {
        moveOrCopyRun(group[0], output, deleteSource);  // Esegue la funzione move_or_copy_run per spostare/copiare la run.
        return;
    }

    // Si crea un vettore e si alloca un RunReader per ogni run del gruppo.
    std::vector<std::unique_ptr<RunReader>> readers;
    readers.reserve(K);
    for (int i = 0; i < K; i++) {
        readers.push_back(std::make_unique<RunReader>(group[i]));  // Inizializza il RunReader per la i-esima run passando il path del file della run
    }

    // Si apre il file di output per la passata di merge.
    FILE* fout = std::fopen(output.c_str(), "wb");
    if (fout == nullptr) {
        throw std::runtime_error("merge_pass: impossibile creare " + output);
    }
    // Si usa un buffer di 8MB per l'output per ridurre le syscall.
    std::setvbuf(fout, nullptr, _IOFBF, 8ULL * 1024 * 1024);

    // Si inizializza una min-heap, usando std::priority_queue con std::greater
    // come comparator, in modo che top() restituisca sempre la HeapEntry con la key minima.
    std::priority_queue<HeapEntry,   // Tipo di elementi
                        std::vector<HeapEntry>, // Tipo di contenitore
                        std::greater<HeapEntry>> heap; // Funzione di confronto

    // Inizializzo la heap con il primo record di ogni run non vuota.
    // Per ognuna delle K run leggo il primo record e lo inserisco nella heap.
    for (int i = 0; i < K; i++) {
        if (!readers[i]->exhausted) {
            HeapEntry entry;
            entry.key     = readers[i]->currentKey;
            entry.runIdx = i;
            heap.push(entry);
        }
    }

    try {
        // Finche la heap non è vuota, ossia ci sono record da processare, estraggo la entry 
        // con la key minima e la scrivo nell'output. Successivamente avanzo il reader corrispondente 
        // e inserisco la nuova entry nella heap.
        while (!heap.empty()) {
            // Estraggo la entry con la key minima.
            HeapEntry top = heap.top();
            heap.pop();
            // Estraggo l'intero puntatore al RunReader corrispondente.
            RunReader* rr = readers[top.runIdx].get();
            // Scrivo il record corrente di questa run nel file di output.
            writeRecord(fout, rr->currentKey, rr->currentLen, rr->payload.data());
            // Avanzo solo la run da cui ho appena estratto il minimo.
            rr->advance();

            // Inserisco il record successivo nella heap se non sono arrivato alla fine della run.
            if (!rr->exhausted) {
                HeapEntry next_entry;
                next_entry.key     = rr->currentKey;
                next_entry.runIdx = top.runIdx;
                heap.push(next_entry);
            }
        }

        // Chiudo il file di output.
        if (std::fclose(fout) != 0) {
            throw std::runtime_error("merge_pass: errore nel flush dell'output");
        }
    } catch (...) {
        std::fclose(fout);  // In caso di eccezione chiudo il file di output prima di propagare l'errore.
        throw;
    }

    // Chiudiamo esplicitamente gli input prima di cancellarli.
    readers.clear();

    // Rimuovo i file di input se richiesto.
    for (int i = 0; i < K; i++) {
        if (deleteSource) {
            if (std::remove(group[i].c_str()) != 0) {
                std::fprintf(stderr, "[WARN] merge_pass: impossibile rimuovere %s\n",
                             group[i].c_str());
            }
        }
    }
}

// kway_merge


/*
Orchestratore del merge multi-pass sequenziale (versione common).
Questa funzione e' usata direttamente dalla versione OpenMP tramite omp_kway_merge
(in omp/include/omp_kway_merger.hpp) e come base di merge_pass() condivisa con FastFlow.
Si ha un parametro merge_fan che indica il numero di run da fondere in parallelo.
Es. con merge_fan = 64:  50 run -> 1 run di output.  200 run -> 4 run di intermezzo -> 1 run di output finale.
Si ha un albero k-ario di run, dove le foglie sono le run di input, i nodi intermedi le run di intermezzo
e la radice e' la run di output.
La versione con parallelismo OMP vive in omp/include/omp_kway_merger.hpp.
La versione con parallelismo FastFlow vive in fastflow/include/ff_kway_merger.hpp.
*/


inline void kwayMerge(
    const std::vector<std::string>& runPaths,    // Vettore di path delle run da fondere.
    const std::string&              outputPath,  // Path del file di output.
    int                             mergeFan    = 64,  // Fan-in massimo: quante run vengono fuse in una singola merge_pass.
    bool                            deleteRuns  = true,  // Indica se cancellare le run sorgente.
    bool                            /*parallelMerge*/ = false) // Ignorato in questa versione seriale; usato dall'API OMP/MPI.
{
    // Se non ci sono run, lancio un errore.
    if (runPaths.empty()) {
        throw std::runtime_error("kway_merge: nessuna run");
    }

    // Se il merge_fan è minore di 2, lo imposto a 2. In questo caso ci sarà solo un merge a 2 vie alla volta.
    if (mergeFan < 2) {
        mergeFan = 2;
    }

    // Caso banale: Se si ha una run singola, basta rinominare o copiare, con la funzione di utility in O(1).
    if (runPaths.size() == 1) {
        moveOrCopyRun(runPaths[0], outputPath, deleteRuns);
        return;
    }

    // Blocco che serve a ricavare la cartella in cui si trovano le run temporanee di input: "/tmp/runs/run_0.bin" -> "/tmp/runs"
    // partendo dal path della prima run.
    std::string tmpDir;
    size_t slashPos = runPaths[0].rfind('/');  // Cerca l'ultima occorrenza di '/' nel path della prima run.
    if (slashPos != std::string::npos) {        // Se l'occorrenza viene trovata.
        tmpDir = runPaths[0].substr(0, slashPos);    // Prende il path della directory temporanea, da inizio stringa all'ultimo slash.
    } else {
        tmpDir = ".";                            // Altrimenti la directory temporanea è la directory corrente.
    }

    // Vettore che contiene le run ancora da fondere.
    // Ad ogni passata viene sostituito da next_level, cioe' dalle run prodotte.
    std::vector<std::string> currentLevel = runPaths;

    int pass = 0; // Contatore del numero di passata corrente. Viene usato per il naming dei file intermedi.

    // While che itera fino a ridurre il numero di run da fondere ad una sola run finale.
    while (currentLevel.size() > 1) {
        ++pass;          // Si incrementa il contatore delle passate.
        const int R = static_cast<int>(currentLevel.size());   // Numero di run ancora da fondere in questa passata.

        // Numero di gruppi indipendenti in questa passata. Es. se merge_fan = 64 e R = 50, num_groups = 1.
        int numGroups = (R + mergeFan - 1) / mergeFan;

        // Preparo i nomi dei file prodotti dalla passata.
        std::vector<std::string> nextLevel(numGroups);    //Vettore di path dei file di output intermedi prodotto dalla passata.
        for (int g = 0; g < numGroups; g++) {  //Si itera per ogni gruppo da fondere.
            if (numGroups == 1) {  //Se c'e' un solo gruppo, si scrive direttamente nell'output finale.
                nextLevel[g] = outputPath;
            } else { //Altrimenti si crea un nome di file intermedio.
                nextLevel[g] = tmpDir + "/run_p" + std::to_string(pass)
                             + "_" + std::to_string(g) + ".bin";
            }
        }

        // Si itera per ogni gruppo di run da fondere.
        for (int g = 0; g < numGroups; g++) {
            int groupStart = g * mergeFan;    //Indice di inizio del gruppo.
            int groupEnd   = std::min(groupStart + mergeFan, R);    //Indice di fine del gruppo.

            // Costruisco il sottovettore del gruppo g.
            std::vector<std::string> group(
                currentLevel.begin() + groupStart, // Puntatore all'inizio del gruppo.
                currentLevel.begin() + groupEnd    // Puntatore alla fine del gruppo.
            );

            bool deleteSource = (pass > 1) || deleteRuns;  //Se pass > 1, cancella le run originali. Altrimenti, se delete_runs è vero, cancella le run originali.
            mergePass(group, nextLevel[g], deleteSource);  // Chiama la funzione merge_pass con il gruppo di run e il path del file di output intermedio.
        }

        currentLevel = std::move(nextLevel);  //Sposto il vettore dei file di output intermedi creati in questa passata nel vettore delle run da fondere per la prossima passata. Finisce il ciclo e si passa all'iterazione successiva.
    }

    // Se current_level ha ancora 1 elemento e non e' gia' output_path, rinominarlo all'output finale.
    if (currentLevel.size() == 1 && currentLevel[0] != outputPath) {
        if (std::rename(currentLevel[0].c_str(), outputPath.c_str()) != 0) {
            throw std::runtime_error("kway_merge: rename finale fallito");
        }
    }
}
