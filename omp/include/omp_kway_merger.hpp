#pragma once
/*
Fase 2 del MergeSort out-of-core in parallelo. 
Estende la logica di kway_merger.hpp introducendo parallelismo a livello di gruppi indipendenti 
tramite task OMP. 
La funzione merge_pass() e' condivisa con kway_merger.hpp (common): 
tutta la logica K-way con min-heap resta invariata, cambia solo 
il modo in cui i gruppi vengono distribuiti ai thread.
*/
#include "kway_merger.hpp"    // merge_pass(), move_or_copy_run() (common)

#include <omp.h>

#include <vector>
#include <string>
#include <stdexcept>
#include <atomic>
#include <algorithm>

/*
Orchestratore del merge multi-pass con parallelismo OpenMP task.
Struttura identica a kway_merge() di kway_merger.hpp, ma usa #pragma omp task
per parallelizzare i gruppi indipendenti di ogni passata.

Parametri:
  run_paths      - Vettore di path delle run da fondere.
  output_path    - Path del file di output finale.
  merge_fan      - Fan-in massimo: quante run vengono fuse in una singola merge_pass.
  delete_runs    - Se true, i file di input vengono rimossi dopo ogni passata.
  parallel_merge - Se true, i gruppi indipendenti vengono eseguiti come task OMP in parallelo.
*/
inline void ompKwayMerge(
    const std::vector<std::string>& runPaths,   // Vettore di path delle run da fondere.
    const std::string&              outputPath, // Path del file di output finale.
    int                             mergeFan      = 64,   // Fan-in massimo.
    bool                            deleteRuns    = true, // Indica se cancellare le run sorgente.
    bool                            parallelMerge = true) // Indica se parallelizzare i gruppi.
{
    // Se non ci sono run, lancio un errore.
    if (runPaths.empty()) {
        throw std::runtime_error("omp_kway_merge: nessuna run");
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

        // Siccome i gruppi sono indipendenti, possono essere fusi in parallelo.
        //Si controlla che il numero di gruppi sia maggiore di 1 e che il merge si voglia parallelizzato.
        if (parallelMerge && numGroups > 1) {
            // Si usa un flag atomico per sapere se un task di merge ha fallito.
            std::atomic<bool> mergeError{false};

            /*
            Si crea la regione parallela. Le variabili condivise sono il vettore delle run da fondere, 
            i nomi dei file prodotti, quanti gruppi di run si fondono in parallelo, il merge fan
            se cancellare le run sorgente, il contatore delle passate, il flag di errore
            e il numero di run da fondere.   
            La parte successiva di codice parte single thread.         
            */
            #pragma omp parallel default(none) \
                shared(currentLevel, nextLevel, numGroups, mergeFan, \
                       deleteRuns, pass, mergeError, R)
            #pragma omp single
            {
                // Creo un task per ogni gruppo di run da fondere in parallelo.
                // Si itera per ogni gruppo
                for (int g = 0; g < numGroups; g++) {
                    int groupStart = g * mergeFan;   //Indice di inizio del gruppo.
                    int groupEnd   = std::min(groupStart + mergeFan, R); //Indice di fine del gruppo.

                    // Costruisco il sottovettore del gruppo g.
                    std::vector<std::string> group(
                        currentLevel.begin() + groupStart,  // Puntatore all'inizio del gruppo.
                        currentLevel.begin() + groupEnd     // Puntatore alla fine del gruppo.
                    );

                    std::string outPath = nextLevel[g]; // Path del file di output del gruppo g.

                    // Al pass 1 decido se cancellare le run originali (delete_runs).
                    // Dai pass successivi i file intermedi vanno sempre cancellati.
                    bool deleteSource = (pass > 1) || deleteRuns;

                    //Lancio un task che esegue la merge pass. 
                    // Le variabili private del task sono group, out_path, del_src. Le variabili condivise sono merge_error.
                    #pragma omp task firstprivate(group, outPath, deleteSource) \
                                     shared(mergeError) default(none)
                    {
                        // Se non c'è stato nessun errore finora, provo ad eseguire la merge pass.
                        if (!mergeError.load(std::memory_order_relaxed)) {  
                            try {
                                mergePass(group, outPath, deleteSource);   //Si chiama la funzione che fa il merge di un gruppo di run.
                            } catch (...) {
                                mergeError.store(true, std::memory_order_relaxed);  //Se c'e' un errore, lo imposto. 
                            }
                        }
                    }
                }
            } // barriera implicita: tutti i task di questa passata sono finiti

            // Controllo se c'e' un errore.
            if (mergeError.load()) {
                throw std::runtime_error("omp_kway_merge: errore in un merge parallelo");
            }

        } else {           //Caso in cui non si ha merge parallelo, poichè si hanno pochi gruppi o parallelismo disabilitato.
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
        }

        currentLevel = std::move(nextLevel);  //Sposto il vettore dei file di output intermedi creati in questa passata nel vettore delle run da fondere per la prossima passata. Finisce il ciclo e si passa all'iterazione successiva.
    }

    // Se current_level ha ancora 1 elemento e non e' gia' output_path, rinominarlo all'output finale.
    if (currentLevel.size() == 1 && currentLevel[0] != outputPath) {
        if (std::rename(currentLevel[0].c_str(), outputPath.c_str()) != 0) {
            throw std::runtime_error("omp_kway_merge: rename finale fallito");
        }
    }
}
