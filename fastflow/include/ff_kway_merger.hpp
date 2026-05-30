#pragma once

// =============================================================================
// ff_kway_merger.hpp - Fase 2 del MergeSort out-of-core (versione FastFlow)
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

Questo modulo e' l'equivalente FastFlow di kway_merger.hpp (versione OpenMP).
kway_merger.hpp    → #pragma omp task  (OMP task pool)
ff_kway_merger.hpp → ff::ParallelFor   (FF work-stealing lock-free)

Perche' questo modulo esiste separatamente:
La versione OpenMP del merge NON e' usabile direttamente in ff_sort.cpp.
FastFlow imposta una CPU affinity (thread pinning) sui suoi thread durante
la Fase 1 (farm). Se il merge parallelo viene eseguito da OpenMP dopo la
farm, i thread OMP ereditano la maschera di affinità ristretta di FastFlow,
finendo tutti sullo stesso core e causando lock contention e crollo delle
performance. Usando ff::ParallelFor, un solo runtime gestisce entrambe le
fasi senza conflitti di affinità.

Architettura del merge parallelo con ff::ParallelFor:
    Per ogni passata (pass):
        num_groups = ceil(R / merge_fan)   gruppi indipendenti
        ff::ParallelFor divide i gruppi tra i worker con work-stealing:
        Worker 0 → merge_pass(group_0, out_0)
        Worker 1 → merge_pass(group_1, out_1)
        ...
    Barriera implicita a fine parallel_for → passata completata.

    La funzione merge_pass() e' condivisa con kway_merger.hpp (common):
    tutta la logica K-way con min-heap resta invariata, cambia solo
    il modo in cui i gruppi vengono distribuiti ai thread.

*/


#include "kway_merger.hpp"    // merge_pass(), move_or_copy_run() (common)

#include <ff/parallel_for.hpp>

#include <vector>
#include <string>
#include <stdexcept>
#include <atomic>
#include <algorithm>
#include <memory>
#include <exception>
#include <mutex>

/*
Orchestratore del merge multi-pass.
Si ha un parametro merge_fan che indica il numero di run da fondere in parallelo.
Es. con merge_fan = 64:  50 run -> 1 run di output.  200 run -> 4 run di intermezzo -> 1 run di output finale.
Si ha un albero k-ario di run, dove le foglie sono le run di input, i nodi intermedi le run di intermezzo
e la radice e' la run di output.
Struttura identica a kway_merge() di kway_merger.hpp, ma usa ff::ParallelFor
al posto di #pragma omp task per parallelizzare i gruppi indipendenti.
Prende in input il vettore di path delle run da fondere, il path del file di output, 
il numero di thread FF da usare nel ParallelFor, quante run vengono fuse per passata e 
un booleano che indica se i file delle run sorgenti vengono cancellati alla fine.
*/
inline void ffKwayMergeLegacy(
    const std::vector<std::string>& runPaths,  // Vettore dei path delle run da fondere
    const std::string&              outputPath, // Path del file di output finale
    int                             nWorkers,   // Numero di thread FF da usare nel ParallelFor
    int                             mergeFan   = 64, // Numero massimo di run da fondere per passata
    bool                            deleteRuns = true) // Se true, i file delle run sorgenti vengono cancellati alla fine
{
    // Se non ci sono run, lancio un errore.
    if (runPaths.empty()) {
        throw std::runtime_error("ff_kway_merge: nessuna run");
    }

    // Se il merge_fan è minore di 2, lo imposto a 2.
    if (mergeFan < 2) {
        mergeFan = 2;
    }

    const bool verbose = mergeVerboseEnabled();
    if (verbose) {
        std::fprintf(stderr,
                     "[merge] impl=fastflow-legacy initialRuns=%zu mergeFan=%d parallelMerge=yes workers=%d\n",
                     runPaths.size(), mergeFan, nWorkers);
    }

    // Caso banale: run singola, basta rinominare o copiare in O(1).
    if (runPaths.size() == 1) {
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=0 runs=1 groups=1 tasks=0 mode=singleRun\n");
        }
        moveOrCopyRun(runPaths[0], outputPath, deleteRuns);
        return;
    }

    // Blocco che ricava la directory temporanea dal path della prima run:
    // es. "/tmp/spm_ff_123/run_0.bin" → "/tmp/spm_ff_123"
    std::string tmpDir;
    size_t slashPos = runPaths[0].rfind('/');  // Cerca l'ultima occorrenza di '/' nel path della prima run.
    if (slashPos != std::string::npos) {   // Se l'occorrenza viene trovata.
        tmpDir = runPaths[0].substr(0, slashPos);     // Prende il path della directory temporanea, da inizio stringa all'ultimo slash.
    } else {
        tmpDir = ".";           // Altrimenti la directory temporanea è la directory corrente.
    }

    // Vettore delle run ancora da fondere.
    // Ad ogni passata viene sostituito da next_level, cioe' dalle run prodotte.
    std::vector<std::string> currentLevel = runPaths;

    int pass = 0; // Contatore del numero di passata corrente. Usato per il naming dei file intermedi.

    // Si istanzia il ParallelFor una volta sola fuori dal while, passando nworkers come numero di thread da usare.
    // Il secondo parametro (false) disabilita il pinning interno di FF:
    // questo evita conflitti con il pinning gia' impostato dalla farm della Fase 1.
    std::unique_ptr<ff::ParallelFor> pf;
    if (runPaths.size() > static_cast<size_t>(mergeFan) && nWorkers > 1) {
        pf = std::make_unique<ff::ParallelFor>(nWorkers, false);
    }

    // While che itera fino a ridurre il numero di run da fondere ad una sola run finale.
    while (currentLevel.size() > 1) {
        ++pass;    // Si incrementa il contatore delle passate.
        const int R = static_cast<int>(currentLevel.size()); // Numero di run ancora da fondere in questa passata.

        // Numero di gruppi indipendenti in questa passata. Es. se merge_fan = 64 e R = 50, num_groups = 1.
        int numGroups = (R + mergeFan - 1) / mergeFan;
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=%d runs=%d groups=%d tasks=%d mode=%s\n",
                         pass, R, numGroups,
                         (numGroups > 1 && pf) ? numGroups : 0,
                         (numGroups > 1 && pf) ? "parallel" : "singleGroup");
        }

        // Preparo i nomi dei file prodotti dalla passata.
        std::vector<std::string> nextLevel(numGroups);    //Vettore di path dei file di output intermedi prodotto dalla passata.
        for (int g = 0; g < numGroups; g++) {   //Si itera per ogni gruppo da fondere.
            if (numGroups == 1) { //Se c'e' un solo gruppo, si scrive direttamente nell'output finale.
                nextLevel[g] = outputPath;
            } else {      //Altrimenti si crea un nome di file intermedio.
                nextLevel[g] = tmpDir + "/run_p" + std::to_string(pass)
                             + "_" + std::to_string(g) + ".bin";
            }
        }

        // Siccome i gruppi sono indipendenti, possono essere fusi in parallelo. Caso in cui abbiamo piu gruppi
        // Se c'e' un solo gruppo, lo eseguiamo direttamente senza overhead di parallelismo.
        if (numGroups > 1 && pf) {
            
            std::atomic<bool> mergeError{false};     // Flag atomico per propagare eccezioni dai thread worker al thread principale.
            std::exception_ptr firstError = nullptr;
            std::mutex errorMutex;

            

/*
Si usa FastFlow ParallelFor per eseguire in parallelo più operazioni di merge durante una passata del k-way merge esterno.
Ogni iterazione del parallel_for prende un gruppo di K file già ordinati e li fonde in un nuovo file temporaneo.
Si ha un vettore currentLevel che contiene i file temporanei da unire, ad esempio: run0.bin run1.bin run2.bin run3.bin run4.bin run5.bin
Se mergeFan = 2, allora i gruppi saranno:
Gruppo 0 -> run0 + run1   Gruppo 1 -> run2 + run3   Gruppo 2 -> run4 + run5
Ogni gruppo viene processato da un thread diverso in parallelo.
Ogni gruppo produce un nuovo file ordinato in nextLevel[g]
*/

/*
parallel_for distribuisce le iterazioni g= [0, num_groups) tra i nworkers thread
con work-stealing. Ogni iterazione corrisponde a un gruppo indipendente.
La chiamata e' bloccante: torna solo quando tutti i gruppi della passata
sono stati fusi. Questo funge da barriera tra una passata e la successiva.
Le variabili catturate per riferimento sono thread-safe perche':
  - current_level e next_level sono in sola lettura durante il parallel_for;
  - merge_error e' un'atomic<bool>;
  - ogni iterazione scrive su un file di output distinto (next_level[g]).
  Ogni worker riceve in input un indice g appartenente all'intervallo [0, num_groups).

            */
            pf->parallel_for(0, numGroups, 1, 0,
                [&](const long g) {                 
                    if (mergeError.load(std::memory_order_relaxed)) return;   // Evito di eseguire merge inutili se un altro gruppo ha gia' fallito.

                    int groupStart = g * mergeFan;   //Indice di inizio del gruppo.
                    int groupEnd   = std::min(groupStart + mergeFan, R);   //Indice di fine del gruppo.

                    // Costruisco il sottovettore del gruppo g.
                    std::vector<std::string> group(
                        currentLevel.begin() + groupStart,  // Puntatore all'inizio del gruppo.
                        currentLevel.begin() + groupEnd     // Puntatore alla fine del gruppo.
                    );

                    // Al pass 1 decido se cancellare le run originali (delete_runs).
                    // Dai pass successivi i file intermedi vanno sempre cancellati.
                    bool deleteSource = (pass > 1) || deleteRuns;

                    try {
                        mergePass(group, nextLevel[g], deleteSource);   // Chiama la funzione merge_pass con il gruppo di run e il path del file di output intermedio.
                    } catch (...) {
                        mergeError.store(true, std::memory_order_relaxed);   // In caso di errore, imposta mergeError a true.
                        std::lock_guard<std::mutex> lock(errorMutex);
                        if (firstError == nullptr) {
                            firstError = std::current_exception();
                        }
                    }
                }
            );

            // Controllo se c'e' un errore in uno dei worker.
            if (firstError != nullptr) {
                std::rethrow_exception(firstError);
            }

        } else {     // Se c'e' un solo gruppo, lo eseguiamo direttamente senza overhead di parallelismo.
            for (int g = 0; g < numGroups; g++) {
                int groupStart = g * mergeFan;    //Indice di inizio del gruppo.
                int groupEnd   = std::min(groupStart + mergeFan, R);    //Indice di fine del gruppo.

                // Costruisco il sottovettore del gruppo g.
                std::vector<std::string> group(
                    currentLevel.begin() + groupStart, // Puntatore all'inizio del gruppo.
                    currentLevel.begin() + groupEnd    // Puntatore alla fine del gruppo.
                );

                bool deleteSource = (pass > 1) || deleteRuns;
                mergePass(group, nextLevel[g], deleteSource);
            }
        }

        currentLevel = std::move(nextLevel); //Sposto il vettore dei file di output intermedi creati in questa passata nel vettore delle run da fondere per la prossima passata. 
    }

    // Se current_level ha ancora 1 elemento e non e' gia' output_path, rinominarlo all'output finale.
    if (currentLevel.size() == 1 && currentLevel[0] != outputPath) {
        if (std::rename(currentLevel[0].c_str(), outputPath.c_str()) != 0) {
            throw std::runtime_error("ff_kway_merge: rename finale fallito");
        }
    }
}

inline std::string ffMergeParentDir(const std::string& path)
{
    size_t slashPos = path.rfind('/');  // Cerca l'ultima occorrenza di '/' nel path della prima run.
    if (slashPos != std::string::npos) {   // Se l'occorrenza viene trovata.
        return path.substr(0, slashPos);     // Prende il path della directory temporanea, da inizio stringa all'ultimo slash.
    }
    return ".";           // Altrimenti la directory temporanea è la directory corrente.
}

inline std::string ffFlatMergeTmpPath(const std::string& tmpDir, int workerId)
{
    return tmpDir + "/run_ff_flat_" + std::to_string(workerId) + ".bin";
}

inline std::vector<std::string> ffMergeGroup(
    const std::vector<std::string>& paths,
    size_t                          begin,
    size_t                          end)
{
    return std::vector<std::string>(paths.begin() + begin, paths.begin() + end);
}

/*
Orchestratore FastFlow del merge flat a due livelli.
La funzione mergePass() resta l'unica primitiva di fusione: questo mantiene
un solo punto in cui vive la logica K-way con min-heap e buffer di I/O.

La nuova strategia non usa mergeFan:
1. Le run vengono divise in blocchi contigui, uno per worker FastFlow.
2. Ogni worker fonde il suo blocco con mergePass() e produce un file intermedio.
3. Il thread principale fonde gli intermedi con una mergePass() finale.

Questa struttura riduce il numero di passate su disco a due. Su cluster HPC e'
vantaggiosa se i file temporanei stanno sullo storage locale del nodo (/tmp o
SLURM_TMPDIR), perche' il collo di bottiglia diventa la banda sequenziale dei
file intermedi e non la creazione di molti livelli di merge.
*/
inline void ffKwayMerge(
    const std::vector<std::string>& runPaths,  // Vettore dei path delle run da fondere
    const std::string&              outputPath, // Path del file di output finale
    int                             nWorkers,   // Numero di thread FF da usare nel ParallelFor
    bool                            deleteRuns = true) // Se true, i file delle run sorgenti vengono cancellati alla fine
{
    // Se non ci sono run, lancio un errore.
    if (runPaths.empty()) {
        throw std::runtime_error("ff_kway_merge: nessuna run");
    }

    if (nWorkers < 1) {
        nWorkers = 1;
    }

    const bool verbose = mergeVerboseEnabled();
    if (verbose) {
        std::fprintf(stderr,
                     "[merge] impl=fastflow-flat initialRuns=%zu workers=%d\n",
                     runPaths.size(), nWorkers);
    }

    // Caso banale: run singola, basta rinominare o copiare in O(1).
    if (runPaths.size() == 1) {
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=0 runs=1 groups=1 tasks=0 mode=singleRun\n");
        }
        moveOrCopyRun(runPaths[0], outputPath, deleteRuns);
        return;
    }

    if (nWorkers == 1) {
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=1 runs=%zu groups=1 tasks=0 mode=singleMergePass\n",
                         runPaths.size());
        }
        mergePass(runPaths, outputPath, deleteRuns);
        return;
    }

    const int workers = std::min<int>(nWorkers, static_cast<int>(runPaths.size()));

    /*
    Soglia HPC per evitare una passata inutile sui dati.
    Se le run sono poche rispetto ai worker disponibili, il primo livello
    parallelo produrrebbe intermedi troppo piccoli e il merge finale
    obbligherebbe a rileggere e riscrivere gli stessi record. In questo caso
    un singolo mergePass() diretto e' piu' efficiente e piu' stabile.
    */
    if (runPaths.size() < 2 * static_cast<size_t>(workers)) {
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=1 runs=%zu groups=1 tasks=0 mode=singleMergePassSmallInput workers=%d\n",
                         runPaths.size(), workers);
        }
        mergePass(runPaths, outputPath, deleteRuns);
        return;
    }

    const size_t groupSize = (runPaths.size() + static_cast<size_t>(workers) - 1)
                           / static_cast<size_t>(workers);
    const std::string tmpDir = ffMergeParentDir(runPaths[0]);

    std::vector<std::string> intermediateFiles(workers);
    for (int worker = 0; worker < workers; worker++) {
        intermediateFiles[worker] = ffFlatMergeTmpPath(tmpDir, worker);
    }

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] level=1 runs=%zu groups=%d tasks=%d mode=parallelFlat groupSize=%zu\n",
                     runPaths.size(), workers, workers, groupSize);
    }

    ff::ParallelFor pf(workers, false);
    std::atomic<bool> mergeError{false};
    std::exception_ptr firstError = nullptr;
    std::mutex errorMutex;

    /*
    Ogni iterazione del parallel_for corrisponde a un worker logico.
    Il worker prende un intervallo contiguo di run gia' ordinate, costruisce
    il vettore group e chiama mergePass() una sola volta.
    Gli output sono distinti, quindi non c'e' contesa tra i worker sui file.
    */
    pf.parallel_for(0, workers, 1, 0,
        [&](const long worker) {
            if (mergeError.load(std::memory_order_relaxed)) return;   // Evito lavori inutili dopo il primo errore.

            const size_t begin = static_cast<size_t>(worker) * groupSize;
            const size_t end = std::min(begin + groupSize, runPaths.size());
            if (begin >= end) return;

            try {
                std::vector<std::string> group = ffMergeGroup(runPaths, begin, end);
                mergePass(group, intermediateFiles[worker], deleteRuns);
            } catch (...) {
                mergeError.store(true, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(errorMutex);
                if (firstError == nullptr) {
                    firstError = std::current_exception();
                }
            }
        }
    );

    if (firstError != nullptr) {
        std::rethrow_exception(firstError);
    }

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] level=2 runs=%d groups=1 tasks=0 mode=finalMergePass\n",
                     workers);
    }

    mergePass(intermediateFiles, outputPath, /*deleteSource=*/true);
}
