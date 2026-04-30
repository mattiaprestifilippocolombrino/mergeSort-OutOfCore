#pragma once
// =============================================================================
// kway_merger.hpp - Fase 2 del MergeSort out-of-core
// =============================================================================
//
// Dopo la fase 1 ho tanti file temporanei ordinati, chiamati "run".
// Questo modulo li fonde fino a produrre un unico file ordinato.
//
// Algoritmo del merge K-way:
//
//   1. apro K run ordinate;
//   2. leggo il primo record di ogni run;
//   3. metto in una min-heap la key del record corrente di ogni run;
//   4. estraggo sempre la key minima;
//   5. scrivo quel record in output;
//   6. avanzo solo la run da cui ho preso il record;
//   7. ripeto finche' la heap e' vuota.
//
// Memoria usata:
//   - un record corrente per run;
//   - un buffer I/O per run;
//   - una heap con al massimo K elementi piccoli.
//
// Se le run totali sono piu' di merge_fan, non le apro tutte insieme:
// faccio merge multi-pass. Questo controlla file descriptor e RAM.
//
// Scelta prestazionale:
// uso merge_fan = 64 di default. Con buffer da 4 MB per run sono circa
// 256 MB, molto meno dei 32 GB per nodo, ma riduco il numero di passate su disco.
// =============================================================================

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

// Buffer per ogni run aperta in merge.
// Il merge fa molte letture sequenziali piccole; il buffer riduce le syscall.
static constexpr size_t MERGE_BUF_SIZE = 4ULL * 1024 * 1024;

// RunReader
//
// Piccolo wrapper per leggere una run ordinata.
// Tiene sempre caricato il "record corrente" della run:
//
//   current_key, current_len, payload
//
// Questo semplifica il merge: la heap guarda solo current_key e run_idx.
// Quando un record viene scritto in output, chiamo advance() solo su quella run.
struct RunReader {
    FILE*             file;         // handle aperto
    uint64_t          current_key;  // key del record corrente (lookahead)
    uint32_t          current_len;  // lunghezza payload corrente
    std::vector<char> payload;      // buffer payload corrente
    bool              exhausted;    // true se la run e' finita

    explicit RunReader(const std::string& path) {
        file         = std::fopen(path.c_str(), "rb");
        current_key  = 0;
        current_len  = 0;
        exhausted    = false;

        if (file == nullptr) {
            throw std::runtime_error("RunReader: impossibile aprire " + path);
        }

        // Lettura sequenziale: buffer grande per ridurre syscall.
        std::setvbuf(file, nullptr, _IOFBF, MERGE_BUF_SIZE);

        // Precarico subito il primo record della run.
        advance();
    }

    // Il distruttore chiude il file anche se esco per eccezione.
    ~RunReader() {
        if (file != nullptr) {
            std::fclose(file);
        }
    }

    // Non copio RunReader: copiare FILE* sarebbe ambiguo.
    RunReader(const RunReader&)            = delete;
    RunReader& operator=(const RunReader&) = delete;

    void advance() {
        RecordHeader hdr;
        bool got_record = read_header(file, hdr);

        // EOF pulito: la run e' finita e non andra' piu' nella heap.
        if (!got_record) {
            exhausted = true;
            return;
        }

        current_key = hdr.key;
        current_len = hdr.len;
        payload.resize(hdr.len);

        // [OTTIMIZZAZIONE] Uso fread_unlocked per abbattere il tempo speso nei lock
        // del mutex per ogni singolo record letto durante il K-way merge.
        size_t bytes_read = fread_unlocked(payload.data(), 1, hdr.len, file);
        if (bytes_read != hdr.len) {
            throw std::runtime_error("RunReader::advance: payload troncato");
        }
    }
};

// Voce della heap.
//
// Non metto payload nella heap: sarebbe costoso e inutile.
// Mi basta sapere:
//   - qual e' la key minima;
//   - da quale run proviene.
struct HeapEntry {
    uint64_t key;
    int      run_idx;

    // Operatore > usato dalla priority_queue per ottenere una min-heap.
    // std::priority_queue e' max-heap di default. Passando std::greater<HeapEntry>
    // come Comparator, l'elemento con key piu' piccola sta sempre in cima.
    bool operator>(const HeapEntry& other) const noexcept {
        return key > other.key;
    }
};

// move_or_copy_run
//
// Usata nei casi banali con una sola run.
// Se posso cancellare i temporanei, rinomino: O(1) e zero I/O.
// Se l'utente ha chiesto --keep-runs, copio per non perdere la run.
inline void move_or_copy_run(const std::string& src,
                             const std::string& dst,
                             bool delete_src)
{
    // Nessuna operazione necessaria se src e dst coincidono.
    if (src == dst) {
        return;
    }

    if (delete_src) {
        // rename e' atomico sul filesystem e non copia i dati: O(1).
        if (std::rename(src.c_str(), dst.c_str()) != 0) {
            throw std::runtime_error("move_or_copy_run: rename fallito");
        }
    } else {
        // Copia il file mantenendo il sorgente (modalita' keep-runs).
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

// merge_pass
//
// Fonde un gruppo di run in un solo file ordinato.
//
// Questa funzione e' indipendente da altri gruppi: se ho, ad esempio, 200 run e
// merge_fan=64, nella prima passata posso fondere gruppi distinti in parallelo.
//
// Costo CPU: O(N log K), dove K e' il numero di run nel gruppo.
// Costo I/O: legge tutti i record del gruppo e li riscrive una volta.
inline void merge_pass(
    const std::vector<std::string>& group,
    const std::string&              output,
    bool                            delete_src)
{
    const int K = static_cast<int>(group.size());

    // Caso banale: una run e' gia' ordinata, non c'e' nulla da fondere.
    if (K == 1) {
        move_or_copy_run(group[0], output, delete_src);
        return;
    }

    // Apro un reader per ogni run del gruppo.
    std::vector<std::unique_ptr<RunReader>> readers;
    readers.reserve(K);
    for (int i = 0; i < K; i++) {
        readers.push_back(std::make_unique<RunReader>(group[i]));
    }

    // File prodotto da questa passata.
    FILE* fout = std::fopen(output.c_str(), "wb");
    if (fout == nullptr) {
        throw std::runtime_error("merge_pass: impossibile creare " + output);
    }
    // Anche l'output e' sequenziale: buffer grande, meno syscall.
    std::setvbuf(fout, nullptr, _IOFBF, 8ULL * 1024 * 1024);

    // Min-heap: std::priority_queue con std::greater ottiene una min-heap,
    // quindi top() restituisce sempre la HeapEntry con la key minima.
    std::priority_queue<HeapEntry,
                        std::vector<HeapEntry>,
                        std::greater<HeapEntry>> heap;

    // Inizializzo la heap con il primo record di ogni run non vuota.
    for (int i = 0; i < K; i++) {
        if (!readers[i]->exhausted) {
            HeapEntry entry;
            entry.key     = readers[i]->current_key;
            entry.run_idx = i;
            heap.push(entry);
        }
    }

    // Invariante: nella heap c'e' al massimo un record corrente per run.
    // Ogni iterazione scrive il minimo globale tra le run ancora attive.
    while (!heap.empty()) {
        // Estraggo la voce con la key minima.
        HeapEntry top = heap.top();
        heap.pop();

        RunReader* rr = readers[top.run_idx].get();

        // Scrivo il record corrente di questa run nell'output della passata.
        write_record(fout, rr->current_key, rr->current_len, rr->payload.data());

        // Avanzo solo la run da cui ho appena estratto il minimo.
        rr->advance();

        // Se la run ha ancora record, reinserisco il nuovo corrente nella heap.
        if (!rr->exhausted) {
            HeapEntry next_entry;
            next_entry.key     = rr->current_key;
            next_entry.run_idx = top.run_idx;
            heap.push(next_entry);
        }
    }

    if (std::fclose(fout) != 0) {
        throw std::runtime_error("merge_pass: errore nel flush dell'output");
    }

    // Chiudiamo esplicitamente gli input prima di cancellarli: e' piu' chiaro
    // per lo studio e funziona anche su filesystem meno permissivi.
    readers.clear();

    // Rimuovo i file sorgente se richiesto.
    for (int i = 0; i < K; i++) {
        if (delete_src) {
            if (std::remove(group[i].c_str()) != 0) {
                std::fprintf(stderr, "[WARN] merge_pass: impossibile rimuovere %s\n",
                             group[i].c_str());
            }
        }
    }
}

// kway_merge
//
// Orchestratore del merge multi-pass.
//
// Esempio con merge_fan=64:
//   50 run  -> 1 passata  -> output
//   200 run -> 4 run      -> 1 run finale
//
// Questo e' il compromesso scelto:
//   - fan-in alto: meno passate su disco;
//   - fan-in limitato: RAM e file descriptor controllati;
//   - gruppi indipendenti: parallelismo semplice con task OpenMP.
inline void kway_merge(
    const std::vector<std::string>& run_paths,
    const std::string&              output_path,
    int                             merge_fan      = 64,
    bool                            delete_runs    = true,
    bool                            parallel_merge = true)
{
    if (run_paths.empty()) {
        throw std::runtime_error("kway_merge: nessuna run");
    }

    // Previene loop infinito con fan-in troppo piccolo.
    if (merge_fan < 2) {
        merge_fan = 2;
    }

    // Caso banale: run singola, quindi basta rinominare o copiare.
    if (run_paths.size() == 1) {
        move_or_copy_run(run_paths[0], output_path, delete_runs);
        return;
    }

    // Ricava la directory temporanea dal path della prima run.
    std::string tmp_dir;
    size_t slash_pos = run_paths[0].rfind('/');
    if (slash_pos != std::string::npos) {
        tmp_dir = run_paths[0].substr(0, slash_pos);
    } else {
        tmp_dir = ".";
    }

    // current_level contiene le run ancora da fondere.
    // A ogni passata viene sostituito da next_level, cioe' dalle run prodotte.
    std::vector<std::string> current_level = run_paths;

    int pass = 0; // numero di passata corrente (per naming dei file)

    // Loop fino a ridurre a una sola run.
    while (current_level.size() > 1) {
        ++pass;
        const int R = static_cast<int>(current_level.size());

        // Numero di gruppi indipendenti in questa passata.
        int num_groups = (R + merge_fan - 1) / merge_fan;

        // Preparo i nomi dei file prodotti dalla passata.
        std::vector<std::string> next_level(num_groups);
        for (int g = 0; g < num_groups; g++) {
            if (num_groups == 1) {
                // Questa e' l'ultima passata: scrivo direttamente nell'output finale.
                next_level[g] = output_path;
            } else {
                next_level[g] = tmp_dir + "/run_p" + std::to_string(pass)
                             + "_" + std::to_string(g) + ".bin";
            }
        }

        // I gruppi sono indipendenti: posso fonderli in parallelo.
        if (parallel_merge && num_groups > 1) {
            // Uso un flag atomico per sapere se un task di merge ha fallito.
            std::atomic<bool> merge_error{false};

            #pragma omp parallel default(none) \
                shared(current_level, next_level, num_groups, merge_fan, \
                       delete_runs, pass, merge_error, R)
            #pragma omp single
            {
                for (int g = 0; g < num_groups; g++) {
                    int group_start = g * merge_fan;
                    int group_end   = std::min(group_start + merge_fan, R);

                    // Costruisco il sottovettore del gruppo g.
                    std::vector<std::string> group(
                        current_level.begin() + group_start,
                        current_level.begin() + group_end
                    );

                    std::string out_path = next_level[g];

                    // Al pass 1 decido se cancellare le run originali (delete_runs).
                    // Dai pass successivi i file intermedi vanno sempre cancellati.
                    bool del_src = (pass > 1) || delete_runs;

                    #pragma omp task firstprivate(group, out_path, del_src) \
                                     shared(merge_error) default(none)
                    {
                        if (!merge_error.load(std::memory_order_relaxed)) {
                            try {
                                merge_pass(group, out_path, del_src);
                            } catch (...) {
                                merge_error.store(true, std::memory_order_relaxed);
                            }
                        }
                    }
                }
            } // barriera implicita: tutti i task di questa passata sono finiti

            if (merge_error.load()) {
                throw std::runtime_error("kway_merge: errore in un merge parallelo");
            }

        } else {
            // Caso semplice: pochi gruppi o parallelismo disabilitato.
            for (int g = 0; g < num_groups; g++) {
                int group_start = g * merge_fan;
                int group_end   = std::min(group_start + merge_fan, R);

                std::vector<std::string> group(
                    current_level.begin() + group_start,
                    current_level.begin() + group_end
                );

                bool del_src = (pass > 1) || delete_runs;
                merge_pass(group, next_level[g], del_src);
            }
        }

        current_level = std::move(next_level);
    }

    // Se current_level ha ancora 1 elemento e non e' gia' output_path,
    // rinominarlo all'output finale.
    if (current_level.size() == 1 && current_level[0] != output_path) {
        if (std::rename(current_level[0].c_str(), output_path.c_str()) != 0) {
            throw std::runtime_error("kway_merge: rename finale fallito");
        }
    }
}
