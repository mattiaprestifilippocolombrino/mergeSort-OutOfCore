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
#include <exception>

inline std::string ompMergeParentDir(const std::string& path)
{
    size_t slashPos = path.rfind('/');  // Cerca l'ultima occorrenza di '/' nel path della prima run.
    if (slashPos != std::string::npos) {        // Se l'occorrenza viene trovata.
        return path.substr(0, slashPos);    // Prende il path della directory temporanea, da inizio stringa all'ultimo slash.
    }
    return ".";                            // Altrimenti la directory temporanea è la directory corrente.
}

inline std::string ompFlatMergeTmpPath(const std::string& tmpDir, int workerId)
{
    return tmpDir + "/run_omp_flat_" + std::to_string(workerId) + ".bin";
}

inline std::vector<std::string> ompMergeGroup(
    const std::vector<std::string>& paths,
    size_t                          begin,
    size_t                          end)
{
    return std::vector<std::string>(paths.begin() + begin, paths.begin() + end);
}

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
inline void ompKwayMergeLegacy(
    const std::vector<std::string>& runPaths,   // Vettore di path delle run da fondere.
    const std::string&              outputPath, // Path del file di output finale.
    int                             mergeFan      = 64,   // Fan-in massimo.
    bool                            deleteRuns    = true, // Indica se cancellare le run sorgente.
    bool                            parallelMerge = true) // Indica se parallelizzare i gruppi.
{
    // Se non ci sono run, lancio un errore.
    if (runPaths.empty()) {
        throw std::runtime_error("omp_kway_merge_multipass: nessuna run");
    }

    // Se il merge_fan è minore di 2, lo imposto a 2.
    if (mergeFan < 2) {
        mergeFan = 2;
    }

    const bool verbose = mergeVerboseEnabled();
    if (verbose) {
        std::fprintf(stderr,
                     "[merge] impl=omp-multipass initialRuns=%zu mergeFan=%d parallelMerge=%s\n",
                     runPaths.size(), mergeFan, parallelMerge ? "yes" : "no");
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

    // Blocco che serve a ricavare la cartella in cui si trovano le run temporanee di input: "/tmp/runs/run_0.bin" -> "/tmp/runs"
    // partendo dal path della prima run.
    std::string tmpDir = ompMergeParentDir(runPaths[0]);

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
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=%d runs=%d groups=%d tasks=%d mode=%s\n",
                         pass, R, numGroups,
                         (parallelMerge && numGroups > 1) ? numGroups : 0,
                         (parallelMerge && numGroups > 1) ? "parallel" : "singleGroup");
        }

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
            std::exception_ptr firstError = nullptr;

            /*
            Si crea la regione parallela. Le variabili condivise sono il vettore delle run da fondere, 
            i nomi dei file prodotti, quanti gruppi di run si fondono in parallelo, il merge fan
            se cancellare le run sorgente, il contatore delle passate, il flag di errore
            e il numero di run da fondere.   
            La parte successiva di codice parte single thread.         
            */
            #pragma omp parallel default(none) \
                shared(currentLevel, nextLevel, numGroups, mergeFan, \
                       deleteRuns, pass, mergeError, firstError, R)
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
                                     shared(mergeError, firstError) default(none)
                    {
                        // Se non c'è stato nessun errore finora, provo ad eseguire la merge pass.
                        if (!mergeError.load(std::memory_order_relaxed)) {  
                            try {
                                mergePass(group, outPath, deleteSource);   //Si chiama la funzione che fa il merge di un gruppo di run.
                            } catch (...) {
                                mergeError.store(true, std::memory_order_relaxed);  //Se c'e' un errore, lo imposto. 
                                #pragma omp critical(omp_merge_multipass_error)
                                {
                                    if (firstError == nullptr) {
                                        firstError = std::current_exception();
                                    }
                                }
                            }
                        }
                    }
                }
            } // barriera implicita: tutti i task di questa passata sono finiti

            // Controllo se c'e' un errore.
            if (firstError != nullptr) {
                std::rethrow_exception(firstError);
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

        currentLevel = std::move(nextLevel);  //Sposto il vettore dei file di output intermedi creati dalla passata nel vettore delle run da fondere per la prossima passata. Finisce il ciclo e si passa all'iterazione successiva.
    }

    // Se current_level ha ancora 1 elemento e non e' gia' output_path, rinominarlo all'output finale.
    if (currentLevel.size() == 1 && currentLevel[0] != outputPath) {
        if (std::rename(currentLevel[0].c_str(), outputPath.c_str()) != 0) {
            throw std::runtime_error("omp_kway_merge_multipass: rename finale fallito");
        }
    }
}

/*
Orchestratore del merge flat con parallelismo OpenMP.
Questa versione e' pensata per il cluster HPC quando i file temporanei stanno
su storage locale del nodo, ad esempio /tmp o SLURM_TMPDIR.

La funzione mergePass() resta l'unica primitiva di fusione:
tutta la logica K-way con min-heap resta invariata, cambia solo
il modo in cui le run vengono distribuite ai thread.

La strategia non usa merge_fan:
1. Le run vengono divise in blocchi contigui, uno per thread.
2. Ogni thread chiama mergePass() sul proprio blocco e produce un file intermedio.
3. Il thread principale chiama mergePass() sugli intermedi finali.

In questo modo il merge usa al massimo due passate su disco:
una passata parallela dalle run agli intermedi e una passata finale dagli
intermedi al file di output.
*/
inline void ompKwayMerge(
    const std::vector<std::string>& runPaths,   // Vettore di path delle run da fondere.
    const std::string&              outputPath, // Path del file di output finale.
    bool                            deleteRuns    = true, // Indica se cancellare le run sorgente.
    bool                            parallelMerge = true) // Indica se parallelizzare il primo livello.
{
    // Se non ci sono run, lancio un errore.
    if (runPaths.empty()) {
        throw std::runtime_error("omp_kway_merge: nessuna run");
    }

    const bool verbose = mergeVerboseEnabled();
    const int maxThreads = std::max(1, omp_get_max_threads());

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] impl=omp-flat initialRuns=%zu maxThreads=%d parallelMerge=%s\n",
                     runPaths.size(), maxThreads, parallelMerge ? "yes" : "no");
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

    // Se il merge parallelo e' disabilitato, faccio direttamente un K-way merge unico.
    if (!parallelMerge || maxThreads == 1) {
        if (verbose) {
            std::fprintf(stderr,
                         "[merge] level=1 runs=%zu groups=1 tasks=0 mode=singleMergePass\n",
                         runPaths.size());
        }
        mergePass(runPaths, outputPath, deleteRuns);
        return;
    }

    // Uso al massimo un thread per run. Se ci sono meno run dei thread,
    // i thread in eccesso non vengono attivati per evitare lavoro vuoto.
    const int workers = std::min<int>(maxThreads, static_cast<int>(runPaths.size()));

    /*
    Soglia HPC per evitare una passata inutile sui dati.
    Se le run sono poche rispetto ai thread disponibili, il primo livello
    parallelo produrrebbe gruppi da 1-2 run e costringerebbe comunque a una
    seconda passata finale sugli intermedi. In quel caso un solo mergePass()
    diretto legge e scrive i dati una volta sola ed e' piu' efficiente anche
    su /tmp in RAM.
    */
    if (runPaths.size() <= 2 * static_cast<size_t>(workers)) {
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
    const std::string tmpDir = ompMergeParentDir(runPaths[0]);

    const int groups = static_cast<int>(
        (runPaths.size() + groupSize - 1) / groupSize
    );

    // Preparo un file intermedio per ogni gruppo reale, non per ogni worker.
    // Con groupSize arrotondato per eccesso possono esserci meno gruppi dei
    // thread richiesti; passare file non creati al merge finale causa crash.
    std::vector<std::string> intermediateFiles(groups);
    for (int group = 0; group < groups; group++) {
        intermediateFiles[group] = ompFlatMergeTmpPath(tmpDir, group);
    }

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] level=1 runs=%zu groups=%d tasks=%d mode=parallelFlat groupSize=%zu\n",
                     runPaths.size(), groups, groups, groupSize);
    }

    std::atomic<bool> mergeError{false};
    std::exception_ptr firstError = nullptr;

    /*
    Ogni iterazione del parallel for assegna a un thread un intervallo contiguo
    di run gia' ordinate. Gli output sono distinti, quindi non c'e' contesa sui
    file intermedi. In caso di errore, salvo la prima eccezione e la rilancio
    nel thread principale dopo la barriera implicita del parallel for.
    */
    #pragma omp parallel for schedule(static) default(none) \
        shared(runPaths, intermediateFiles, groupSize, groups, \
               deleteRuns, mergeError, firstError)
    for (int groupIdx = 0; groupIdx < groups; groupIdx++) {
        const size_t begin = static_cast<size_t>(groupIdx) * groupSize;
        const size_t end = std::min(begin + groupSize, runPaths.size());

        if (begin >= end || mergeError.load(std::memory_order_relaxed)) {
            continue;
        }

        try {
            std::vector<std::string> group = ompMergeGroup(runPaths, begin, end);
            mergePass(group, intermediateFiles[groupIdx], deleteRuns);
        } catch (...) {
            mergeError.store(true, std::memory_order_relaxed);
            #pragma omp critical(omp_merge_flat_error)
            {
                if (firstError == nullptr) {
                    firstError = std::current_exception();
                }
            }
        }
    }

    if (firstError != nullptr) {
        std::rethrow_exception(firstError);
    }

    if (verbose) {
        std::fprintf(stderr,
                     "[merge] level=2 runs=%d groups=1 tasks=0 mode=finalMergePass\n",
                     groups);
    }

    // Il merge finale cancella sempre gli intermedi prodotti dal primo livello.
    mergePass(intermediateFiles, outputPath, /*deleteSource=*/true);
}
