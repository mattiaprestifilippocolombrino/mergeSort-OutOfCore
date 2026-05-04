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
inline void ffKwayMerge(
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

    // Caso banale: run singola, basta rinominare o copiare in O(1).
    if (runPaths.size() == 1) {
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
    ff::ParallelFor pf(nWorkers, false);

    // While che itera fino a ridurre il numero di run da fondere ad una sola run finale.
    while (currentLevel.size() > 1) {
        ++pass;    // Si incrementa il contatore delle passate.
        const int R = static_cast<int>(currentLevel.size()); // Numero di run ancora da fondere in questa passata.

        // Numero di gruppi indipendenti in questa passata. Es. se merge_fan = 64 e R = 50, num_groups = 1.
        int numGroups = (R + mergeFan - 1) / mergeFan;

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
        if (numGroups > 1) {
            
            std::atomic<bool> mergeError{false};     // Flag atomico per propagare eccezioni dai thread worker al thread principale.

            

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
            pf.parallel_for(0, numGroups, 1, 0,
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
                    }
                }
            );

            // Controllo se c'e' un errore in uno dei worker.
            if (mergeError.load()) {
                throw std::runtime_error("ff_kway_merge: errore in un merge parallelo");
            }

        } else {     // Se c'e' un solo gruppo, lo eseguiamo direttamente senza overhead di parallelismo.
            bool deleteSource = (pass > 1) || deleteRuns;
            mergePass(currentLevel, nextLevel[0], deleteSource);
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
