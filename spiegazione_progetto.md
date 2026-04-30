# Out-of-Core MergeSort: Spiegazione del Progetto

Questo progetto implementa un algoritmo di ordinamento **Out-of-Core MergeSort** per record a lunghezza variabile. L'obiettivo è ordinare dataset molto grandi (potenzialmente superiori alla RAM disponibile nel sistema) utilizzando tre diversi approcci di parallelizzazione per l'HPC (High Performance Computing):
1.  **OpenMP** (Single Node)
2.  **FastFlow** (Single Node)
3.  **MPI + OpenMP** (Multi Node / Distribuito)

## Architettura dell'Algoritmo

Indipendentemente dalla tecnologia di parallelizzazione usata, l'algoritmo si divide sempre in due fasi principali:

### Fase 1: Creazione delle Run (Sort)
Il file di input originale (troppo grande per la RAM) viene letto sequenzialmente in blocchi (chunk) di dimensione fissa (es. 256 MB).
Ogni blocco viene caricato in memoria, ordinato per chiave (utilizzando `std::sort` su un indice leggero per evitare di spostare i grandi payload associati alle chiavi) e infine scritto su disco come file temporaneo, chiamato **"run"**.
Alla fine di questa fase, si avranno multipli file "run" (es. `run_0.bin`, `run_1.bin`, ...), ognuno dei quali è localmente ordinato.

### Fase 2: K-Way Merge
Una volta generate le "run" ordinate, si passa all'unione (merge) per creare il file ordinato finale.
Poiché non si possono tenere tutti i file aperti contemporaneamente (limitazioni sui file descriptor e RAM), si esegue un **merge multi-passato (multi-pass)**:
- Si aprono al massimo $K$ file "run" alla volta (chiamato `merge-fan`, di default 64).
- Si utilizza una struttura dati "Min-Heap" per estrarre costantemente il record con la chiave minore tra i $K$ file correnti, scrivendolo in un nuovo file (più grande) e avanzando la lettura solo dal file da cui si è estratto.
- Questo processo riduce progressivamente il numero di file fino a ottenerne uno solo: il file di output completamente ordinato.

## Formato dei Dati (Record)
Ogni record è salvato in modo binario sul disco ed è composto da:
-   `key` (8 byte, `uint64_t`): la chiave utilizzata per l'ordinamento.
-   `len` (4 byte, `uint32_t`): la lunghezza del payload.
-   `payload` (`len` byte, sequenza binaria opaca): i dati associati alla chiave (max dimensione `PAYLOAD_MAX`, default 4096).

## Versioni Implementate

### 1. OpenMP (`omp_sort.cpp`)
-   **Lettura:** Un singolo thread (`#pragma omp single`) legge l'input in chunk sequenzialmente.
-   **Fase 1 (Sort):** Ogni chunk viene affidato a un task OpenMP (`#pragma omp task`) che lo ordina e salva la run su disco in parallelo.
-   **Fase 2 (Merge):** Il merge multi-pass raggruppa le run. Più gruppi possono essere "fusi" (merged) in parallelo usando i task OpenMP.

### 2. FastFlow (`ff_sort.cpp`)
-   **Fase 1 (Sort):** Utilizza il pattern **Farm** di FastFlow.
    -   L'**Emitter** (che non partecipa al lavoro pesante) legge i chunk dal disco e li immette nella coda della Farm.
    -   I **Worker** prelevano i chunk dalla coda, li ordinano e scrivono le run. Il framework FastFlow gestisce il bilanciamento del carico tramite code lock-free (SPSC). Non c'è alcun Collector, i worker scrivono direttamente su file.
-   **Fase 2 (Merge):** Condivide l'implementazione del K-way merge con la versione OpenMP, ma viene disabilitato il parallelismo in questa fase per evitare conflitti con la "thread affinity" (il pinning dei thread ai core) impostata da FastFlow.

### 3. MPI + OpenMP (`mpi_sort.cpp`)
-   **Fase 1 (Sort Locale):**
    -   Il processo Master (Rank 0) "esplora" il file per trovare dei confini esatti tra i record (boundary) e divide il file in $P$ porzioni logiche uguali (dove $P$ è il numero di rank). Comunica questi limiti tramite `MPI_Bcast`.
    -   Ogni nodo legge la propria porzione, generando e ordinando i propri chunk localmente usando i task OpenMP (in pratica esegue la Fase 1 e la Fase 2 localmente, producendo un singolo file ordinato per quel nodo: `local_sorted.bin`).
-   **Fase 2 (Merge Distribuito - Binary Tree Merge):**
    -   I nodi fondono i loro file locali `local_sorted.bin` in un file globale usando una struttura ad albero binario per evitare che il Master diventi un collo di bottiglia.
    -   Allo step $S$ (1, 2, 4, 8...), un nodo invia (via rete con `MPI_Send` a blocchi da 256MB) il suo file al nodo vicino. Il ricevente usa `MPI_Recv`, salva il file, e poi fa un K-Way merge (a 2 vie) tra il suo file e quello appena ricevuto.
    -   Dopo $\log_2(P)$ passi, il nodo 0 avrà accumulato il file finale ordinato, rinominandolo nel file di output richiesto.

---

## Ordine di Studio dei Moduli

Per comprendere appieno il progetto, ti consiglio di studiare il codice sorgente nel seguente ordine logico (partendo dalle basi comuni e procedendo verso le implementazioni specifiche):

### 1. Fondamenta e Strutture Dati (Directory: `common/include/`)
*   **`record.hpp`**: **Parti da qui.** Definisce cos'è un record (header `key`+`len` e payload) e le funzioni base (ottimizzate `fread_unlocked` e `fwrite_unlocked`) per l'I/O. Fondamentale per capire cosa l'algoritmo sta muovendo.
*   **`temp_dir.hpp`**: Piccolo helper RAII per gestire la directory temporanea dove si salvano le run (creazione ed eliminazione automatica).

### 2. Algoritmo Core (Directory: `common/include/`)
*   **`chunk_sorter.hpp`** (Fase 1): Guarda la struttura `RecordIndex`. Nota come il payload *non* venga spostato in RAM durante la funzione `std::sort`. Qui trovi anche la parallelizzazione OMP (`sort_range_to_runs`).
*   **`kway_merger.hpp`** (Fase 2): Studia `RunReader` e la `priority_queue` (Min-Heap). Capisci il trucco del multi-pass e la logica generale di come $K$ file ordinati diventano uno.

### 3. Programmi Ausiliari (Directory: `common/src/`)
*   **`generate.cpp`**: Come viene costruito un file di test binario casuale o già ordinato.
*   **`verify.cpp`**: (Opzionale) Capisci come si valida l'ordinamento e la consistenza calcolando l'hash FNV-1a.

### 4. Le Varianti (Directory: `omp/src/`, `fastflow/src/`, `mpi/src/`)
*   **`omp_sort.cpp`**: È il `main()` della versione OpenMP. Connette `sort_to_runs` e `kway_merge`. Molto lineare.
*   **`ff_chunk_sorter.hpp`** (in `fastflow/include/`): Guarda come l'`FFEmitter` fa lo stesso lavoro del thread `single` in OMP e inoltra il `ChunkData` ai `FFWorker` che riutilizzano la funzione di ordinamento di OMP.
*   **`ff_sort.cpp`**: Il `main()` della versione FastFlow. Modello a Farm, privo del Collector.
*   **`mpi_sort.cpp`**: È la versione più complessa e istruttiva. Guarda la funzione `compute_record_boundaries` e soprattutto la logica dell'albero di riduzione per fare l'invio rete e il merge distribuito (`mpi_send_file`, `mpi_recv_file`).
