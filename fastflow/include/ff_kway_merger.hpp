#pragma once
// =============================================================================
// ff_kway_merger.hpp - Fase 2 del MergeSort out-of-core (versione FastFlow)
// =============================================================================
//
// Questo modulo e' l'equivalente FastFlow di kway_merger.hpp (versione OpenMP).
// La differenza e' nel meccanismo di parallelismo usato per i gruppi indipendenti
// di ogni passata:
//
//   kway_merger.hpp    → #pragma omp task  (OMP task pool)
//   ff_kway_merger.hpp → ff::ParallelFor   (FF work-stealing lock-free)
//
// Perche' questo modulo esiste separatamente:
//   La versione OpenMP del merge NON e' usabile direttamente in ff_sort.cpp.
//   FastFlow imposta una CPU affinity (thread pinning) sui suoi thread durante
//   la Fase 1 (farm). Se il merge parallelo viene eseguito da OpenMP dopo la
//   farm, i thread OMP ereditano la maschera di affinità ristretta di FastFlow,
//   finendo tutti sullo stesso core e causando lock contention e crollo delle
//   performance. Usando ff::ParallelFor, un solo runtime gestisce entrambe le
//   fasi senza conflitti di affinità.
//
// Architettura del merge parallelo con ff::ParallelFor:
//
//   Per ogni passata (pass):
//     num_groups = ceil(R / merge_fan)   gruppi indipendenti
//
//     ff::ParallelFor divide i gruppi tra i worker con work-stealing:
//       Worker 0 → merge_pass(group_0, out_0)
//       Worker 1 → merge_pass(group_1, out_1)
//       ...
//     Barriera implicita a fine parallel_for → passata completata.
//
//   La funzione merge_pass() e' condivisa con kway_merger.hpp (common):
//   tutta la logica K-way con min-heap resta invariata, cambia solo
//   il modo in cui i gruppi vengono distribuiti ai thread.
//
// =============================================================================

#include "kway_merger.hpp"    // merge_pass(), move_or_copy_run() (common)

#include <ff/parallel_for.hpp>

#include <vector>
#include <string>
#include <stdexcept>
#include <atomic>
#include <algorithm>

/*
Orchestratore del merge multi-pass con parallelismo FastFlow.
Struttura identica a kway_merge() di kway_merger.hpp, ma usa ff::ParallelFor
al posto di #pragma omp task per parallelizzare i gruppi indipendenti.

Parametri:
  run_paths    - Vettore di path delle run da fondere.
  output_path  - Path del file di output finale.
  nworkers     - Numero di thread FF da usare nel ParallelFor.
  merge_fan    - Fan-in massimo: quante run vengono fuse in una singola merge_pass.
  delete_runs  - Se true, i file di input vengono rimossi dopo ogni passata.
*/
inline void ffKwayMerge(
    const std::vector<std::string>& runPaths,
    const std::string&              outputPath,
    int                             nWorkers,
    int                             mergeFan   = 64,
    bool                            deleteRuns = true)
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
    size_t slashPos = runPaths[0].rfind('/');
    if (slashPos != std::string::npos) {
        tmpDir = runPaths[0].substr(0, slashPos);
    } else {
        tmpDir = ".";
    }

    // Vettore delle run ancora da fondere.
    // Ad ogni passata viene sostituito da next_level, cioe' dalle run prodotte.
    std::vector<std::string> currentLevel = runPaths;

    int pass = 0; // Contatore del numero di passata corrente. Usato per il naming dei file intermedi.

    // Istanzio il ParallelFor una volta sola fuori dal while.
    // Il secondo parametro (false) disabilita il pinning interno di FF:
    // questo evita conflitti con il pinning gia' impostato dalla farm della Fase 1.
    ff::ParallelFor pf(nWorkers, false);

    // While che itera fino a ridurre il numero di run da fondere ad una sola run finale.
    while (currentLevel.size() > 1) {
        ++pass;
        const int R = static_cast<int>(currentLevel.size()); // Numero di run ancora da fondere in questa passata.

        // Numero di gruppi indipendenti in questa passata.
        // Es. se merge_fan = 64 e R = 50, num_groups = 1.
        int numGroups = (R + mergeFan - 1) / mergeFan;

        // Preparo i nomi dei file prodotti dalla passata.
        std::vector<std::string> nextLevel(numGroups);
        for (int g = 0; g < numGroups; g++) {
            if (numGroups == 1) { // Unico gruppo: scrivo direttamente nell'output finale.
                nextLevel[g] = outputPath;
            } else { // Piu' gruppi: creo file intermedi nominati per passata e indice.
                nextLevel[g] = tmpDir + "/run_p" + std::to_string(pass)
                             + "_" + std::to_string(g) + ".bin";
            }
        }

        // Siccome i gruppi sono indipendenti, possono essere fusi in parallelo.
        // Se c'e' un solo gruppo, lo eseguiamo direttamente senza overhead di parallelismo.
        if (numGroups > 1) {
            // Flag atomico per propagare eccezioni dai thread worker al thread principale.
            std::atomic<bool> mergeError{false};

            /*
            parallel_for distribuisce le iterazioni [0, num_groups) tra i nworkers thread
            con work-stealing. Ogni iterazione corrisponde a un gruppo indipendente.
            La chiamata e' bloccante: torna solo quando tutti i gruppi della passata
            sono stati fusi. Questo funge da barriera tra una passata e la successiva.
            Le variabili catturate per riferimento sono thread-safe perche':
              - current_level e next_level sono in sola lettura durante il parallel_for;
              - merge_error e' un'atomic<bool>;
              - ogni iterazione scrive su un file di output distinto (next_level[g]).
            */
            pf.parallel_for(0, numGroups, 1, 0,
                [&](const long g) {
                    // Evito di eseguire merge inutili se un altro gruppo ha gia' fallito.
                    if (mergeError.load(std::memory_order_relaxed)) return;

                    int groupStart = g * mergeFan;
                    int groupEnd   = std::min(groupStart + mergeFan, R);

                    // Costruisco il sottovettore del gruppo g.
                    std::vector<std::string> group(
                        currentLevel.begin() + groupStart,
                        currentLevel.begin() + groupEnd
                    );

                    // Al pass 1 decido se cancellare le run originali (delete_runs).
                    // Dai pass successivi i file intermedi vanno sempre cancellati.
                    bool deleteSource = (pass > 1) || deleteRuns;

                    try {
                        mergePass(group, nextLevel[g], deleteSource);
                    } catch (...) {
                        mergeError.store(true, std::memory_order_relaxed);
                    }
                }
            );

            // Controllo se c'e' un errore in uno dei worker.
            if (mergeError.load()) {
                throw std::runtime_error("ff_kway_merge: errore in un merge parallelo");
            }

        } else {
            // Caso sequenziale: un solo gruppo, nessun overhead di parallelismo.
            bool deleteSource = (pass > 1) || deleteRuns;
            mergePass(currentLevel, nextLevel[0], deleteSource);
        }

        currentLevel = std::move(nextLevel); // Passo al livello successivo dell'albero di merge.
    }

    // Se current_level ha ancora 1 elemento e non e' gia' output_path, rinominarlo all'output finale.
    if (currentLevel.size() == 1 && currentLevel[0] != outputPath) {
        if (std::rename(currentLevel[0].c_str(), outputPath.c_str()) != 0) {
            throw std::runtime_error("ff_kway_merge: rename finale fallito");
        }
    }
}
