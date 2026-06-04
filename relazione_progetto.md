# Relazione didattica sul progetto

## 1. Introduzione

Il progetto implementa un algoritmo di ordinamento **out-of-core** per record binari a lunghezza variabile. L'obiettivo e' ordinare file molto grandi, potenzialmente piu' grandi della memoria RAM disponibile, usando tecniche di parallelismo adatte a sistemi multicore e distribuiti.

Il problema affrontato e' tipico dell'elaborazione di grandi moli di dati: se il dataset non puo' essere caricato interamente in memoria, non e' possibile applicare direttamente un normale `std::sort` su tutti i record. Serve quindi un algoritmo che lavori a blocchi, usando il disco come memoria esterna e mantenendo in RAM solo una porzione controllata del file.

Per confrontare approcci diversi di parallelizzazione, il progetto fornisce tre versioni:

1. **OpenMP**, per il parallelismo su singolo nodo.
2. **FastFlow**, sempre su singolo nodo, usando una farm di worker.
3. **MPI + OpenMP**, per l'esecuzione distribuita su piu' processi, con parallelismo locale su ogni rank.

Sono inoltre presenti due programmi di supporto:

- `generate`, che crea file binari di test.
- `verify`, che controlla che l'output sia ordinato e che contenga gli stessi record dell'input.

## 2. Il problema

Il problema consiste nell'ordinare un file binario composto da record indipendenti. Ogni record contiene una chiave numerica e un payload opaco, cioe' una sequenza di byte che deve essere preservata ma non interpretata.

Il formato di ogni record e':

```text
[ key: 8 byte ][ len: 4 byte ][ payload: len byte ]
```

Dove:

- `key` e' un `uint64_t` ed e' la chiave usata per ordinare i record.
- `len` e' un `uint32_t` e indica la dimensione del payload.
- `payload` contiene i dati associati alla chiave.

La difficolta' principale e' che i record hanno dimensione variabile. Questo significa che non si puo' accedere al record numero `i` con una formula semplice, come accadrebbe con record tutti uguali. Per attraversare il file bisogna leggere un header, conoscere `len`, saltare o leggere il payload, e poi passare al record successivo.

Inoltre, per file molto grandi, non e' realistico caricare tutti i record in RAM. Il progetto risolve questo problema usando un **External MergeSort**, cioe' una variante del mergesort pensata per lavorare su memoria esterna.

## 3. Idea generale dell'algoritmo

Tutte le versioni del progetto seguono la stessa struttura logica, divisa in due fasi.

### 3.1 Fase 1: creazione delle run ordinate

Il file di input viene letto sequenzialmente a blocchi, chiamati **chunk**. Ogni chunk contiene un certo numero di record completi. Il codice evita sempre di spezzare un record tra due chunk.

Per ogni chunk:

1. I record vengono copiati in un buffer in memoria.
2. Per ogni record viene creato un indice leggero, chiamato `RecordIndex`.
3. Il vettore degli indici viene ordinato per chiave.
4. I record vengono riscritti su disco seguendo l'ordine degli indici.

Il punto importante e' che durante l'ordinamento non vengono spostati i payload. Ogni elemento `RecordIndex` contiene solo:

```text
key, offset, len
```

Dove:

- `key` serve per confrontare i record.
- `offset` indica dove inizia il record nel buffer del chunk.
- `len` indica la lunghezza del payload.

In questo modo `std::sort` lavora su strutture piccole, mentre i payload restano fermi nel buffer. Questa scelta riduce le copie inutili ed e' particolarmente importante quando i payload sono grandi.

Il risultato della prima fase e' una sequenza di file temporanei ordinati, detti **run**:

```text
run_0.bin
run_1.bin
run_2.bin
...
```

Ogni run e' ordinata internamente, ma il file completo non e' ancora ordinato globalmente.

### 3.2 Fase 2: K-way merge

La seconda fase fonde tutte le run ordinate in un unico file finale ordinato.

Il merge usato e' un **K-way merge**. Invece di fondere solo due file alla volta, l'algoritmo puo' aprire fino a `K` run contemporaneamente, dove `K` e' controllato dal parametro `merge-fan`.

Per ogni run aperta viene mantenuto in memoria solo il record corrente. Le chiavi correnti vengono inserite in una min-heap. A ogni passo:

1. Si estrae dalla heap il record con chiave minima.
2. Lo si scrive nel file di output.
3. Si legge il record successivo dalla stessa run.
4. Si reinserisce la nuova chiave nella heap.

La heap permette di scegliere sempre il record minimo tra le run aperte con costo `O(log K)`.

Se il numero di run e' maggiore di `merge-fan`, il merge viene fatto in piu' passate:

1. Le run vengono divise in gruppi.
2. Ogni gruppo viene fuso in una nuova run intermedia.
3. Le run intermedie vengono fuse di nuovo.
4. Il processo continua fino a ottenere una sola run finale.

Questa tecnica evita di aprire troppi file contemporaneamente e mantiene sotto controllo l'uso della RAM.

### 3.3 Scelta finale: merge multi-pass semplice

Per la versione finale si usa come strategia principale il **merge multi-pass semplice**. Questa scelta e' adatta a un cluster HPC perche' e' prevedibile: non crea thread ausiliari oltre a quelli richiesti a OpenMP/FastFlow, limita naturalmente il numero di file aperti tramite `MERGE_FAN` e mantiene l'uso di memoria sotto controllo.

La variante **Multipass Pipeline** resta disponibile come confronto sperimentale. In quella versione il Merger produce blocchi ordinati e un Writer asincrono li scrive su disco usando un canale SPSC lock-free a due slot. Questa tecnica puo' sovrapporre parte della latenza di scrittura al lavoro di merge, ma introduce thread extra e quindi richiede piu' cautela su Slurm per evitare oversubscription. Per questo motivo non e' la configurazione principale della consegna.

## 4. Componenti comuni

La cartella `common/` contiene il codice condiviso dalle varie versioni.

### 4.1 `record.hpp`

Questo modulo definisce il formato dei record e le funzioni di I/O di base:

- lettura dell'header;
- controllo della lunghezza del payload;
- salto del payload;
- scrittura di un record completo.

Le funzioni usano `fread_unlocked` e `fwrite_unlocked` per ridurre l'overhead delle operazioni di I/O quando l'accesso al file e' svolto da un solo thread.

### 4.2 `chunk_sorter.hpp`

Questo modulo implementa la prima fase dell'algoritmo: lettura dei chunk, costruzione degli indici, ordinamento e scrittura delle run.

La funzione principale e' `sortRangeToRuns`, che puo' ordinare:

- l'intero file, nella versione single-node;
- solo una porzione del file, nella versione MPI.

La funzione `sortToRuns` e' un wrapper per il caso piu' semplice, cioe' l'ordinamento dell'intero input.

### 4.3 `kway_merger.hpp`

Questo modulo implementa il merge K-way comune. Contiene:

- `RunReader`, che legge una run mantenendo caricato il record corrente;
- la min-heap usata per scegliere il record minimo;
- `mergePass`, che fonde un gruppo di run;
- `kwayMerge`, che coordina il merge multi-pass.

### 4.4 `temp_dir.hpp`

Questo modulo gestisce le directory temporanee. Ogni esecuzione crea una sottodirectory distinta, in modo da evitare collisioni tra run generate da processi diversi o da esecuzioni contemporanee.

La directory temporanea viene cancellata automaticamente alla fine, a meno che l'utente non chieda esplicitamente di conservare le run per debug.

## 5. Versione OpenMP

La versione OpenMP e' pensata per un singolo nodo multicore. Il programma principale e' `omp_sort.cpp`.

### 5.1 Parallelismo nella fase di sort

La lettura del file e' sequenziale: un solo thread legge i record e costruisce i chunk. Questo e' naturale, perche' il file va attraversato rispettando i confini dei record.

Una volta riempito un chunk, viene creato un task OpenMP. Il task:

1. ordina gli indici del chunk;
2. scrive la run ordinata su disco;
3. libera la memoria del chunk.

Quindi il parallelismo principale della fase 1 e' sui chunk: mentre il thread lettore continua a produrre lavoro, i worker OpenMP ordinano e scrivono le run gia' pronte.

Per evitare di consumare troppa memoria, il codice limita il numero di chunk "in volo". Senza questo limite, il thread lettore potrebbe produrre molti chunk piu' velocemente di quanto i worker riescano a processarli.

### 5.2 Parallelismo nella fase di merge

La versione OpenMP usa `omp_kway_merger.hpp`, che estende il merge comune parallelizzando i gruppi indipendenti di una stessa passata.

Se in una passata ci sono piu' gruppi da fondere, ogni gruppo puo' essere assegnato a un task OpenMP diverso. Questo parallelismo e' utile soprattutto quando il sistema di I/O riesce a sostenere piu' letture e scritture contemporanee.

### 5.3 Caratteristiche

La versione OpenMP e' la piu' diretta:

- usa un modello a task semplice;
- condivide quasi tutto il codice con la parte comune;
- e' adatta a macchine multicore;
- rappresenta il riferimento principale per correttezza e prestazioni single-node.

## 6. Versione FastFlow

La versione FastFlow e' un'altra implementazione single-node, ma usa un modello di parallelismo diverso. Il programma principale e' `ff_sort.cpp`, mentre la fase di generazione delle run e' in `ff_chunk_sorter.hpp`.

### 6.1 Farm FastFlow

La fase 1 e' implementata come una farm:

```text
Emitter -> Worker 0
        -> Worker 1
        -> Worker 2
        -> ...
```

L'**Emitter** legge il file di input, costruisce i chunk e li invia ai worker.

Ogni **Worker** riceve un `ChunkData`, richiama la stessa funzione di ordinamento usata dalla versione OpenMP, e scrive una run ordinata su disco.

Non c'e' un collector, perche' non serve raccogliere risultati in memoria: ogni worker produce direttamente il proprio file temporaneo.

### 6.2 Merge con FastFlow

Per il merge parallelo la versione FastFlow usa `ff_kway_merger.hpp`, basato su `ff::ParallelFor`.

La logica del K-way merge resta la stessa della versione comune. Cambia solo il modo in cui i gruppi indipendenti vengono distribuiti ai thread.

Questa scelta evita interferenze tra OpenMP e FastFlow, specialmente per quanto riguarda l'affinita' dei thread. Usare un solo runtime per la fase FastFlow rende il comportamento piu' prevedibile.

### 6.3 Caratteristiche

La versione FastFlow e' interessante perche':

- mostra lo stesso algoritmo espresso con un pattern farm;
- separa chiaramente producer e worker;
- riduce la necessita' di gestire manualmente i task;
- permette un confronto con OpenMP sullo stesso problema.

## 7. Versione MPI + OpenMP

La versione MPI + OpenMP estende il progetto a un contesto distribuito. Il programma principale e' `mpi_sort.cpp`.

L'idea e' usare MPI per distribuire il lavoro tra piu' processi e OpenMP per parallelizzare il lavoro locale dentro ogni processo.

### 7.1 Suddivisione del file

Il primo problema della versione MPI e' dividere il file tra i rank senza spezzare i record.

Non e' sufficiente dividere il file in blocchi di byte uguali, perche' un confine potrebbe cadere nel mezzo di un payload. Per questo il rank 0 scorre il file record per record e calcola dei boundary sicuri, cioe' offset che corrispondono sempre all'inizio di un record.

Il vettore dei boundary viene poi distribuito a tutti i rank con `MPI_Bcast`.

Ogni rank riceve quindi una porzione:

```text
[ myStart, myEnd )
```

e puo' ordinarla in modo indipendente.

### 7.2 Sort locale

Ogni rank applica alla propria porzione la stessa logica out-of-core usata nella versione OpenMP:

1. legge chunk della propria stripe;
2. crea run ordinate;
3. fonde le run locali;
4. produce un file `local_sorted.bin`.

Alla fine di questa fase, ogni rank possiede un file ordinato localmente. Il dataset globale pero' non e' ancora ordinato, perche' le chiavi dei diversi rank devono essere fuse.

### 7.3 Merge distribuito ad albero

La seconda fase della versione MPI usa un merge distribuito ad albero binario.

Con 4 rank, ad esempio, lo schema e':

```text
step 1: rank 1 -> rank 0
        rank 3 -> rank 2

step 2: rank 2 -> rank 0
```

A ogni step:

1. alcuni rank diventano sender;
2. altri rank diventano receiver;
3. il sender invia il proprio file ordinato al receiver;
4. il receiver fonde il file ricevuto con il proprio file corrente;
5. il sender termina la partecipazione al merge.

Dopo `log2(P)` passaggi, dove `P` e' il numero di rank, il rank 0 possiede il file ordinato finale.

### 7.4 Invio dei file

I file vengono inviati a blocchi usando MPI. Prima viene comunicata la dimensione totale del file, poi il contenuto viene trasferito in blocchi.

Il codice usa un doppio buffer per sovrapporre, per quanto possibile:

- lettura da disco;
- comunicazione MPI;
- scrittura su disco.

Questa tecnica aiuta a ridurre i tempi morti dovuti a I/O e rete.

### 7.5 Caratteristiche

La versione MPI + OpenMP e' la piu' complessa ma anche la piu' generale:

- permette di usare piu' processi;
- divide il file rispettando i confini dei record;
- usa OpenMP per il sort locale;
- usa MPI per il merge distribuito;
- evita che tutti i dati confluiscano subito sul rank 0 grazie al merge ad albero.

## 8. Generazione e verifica dei dati

Il programma `generate` crea dataset artificiali nel formato binario del progetto. Permette di scegliere:

- numero di record;
- dimensione minima e massima del payload;
- seed del generatore casuale;
- chiavi casuali, ordinate o in ordine inverso.

Il programma `verify` controlla il risultato. In particolare:

1. verifica che le chiavi dell'output siano in ordine non decrescente;
2. confronta input e output usando statistiche aggregate;
3. controlla numero di record, byte totali e hash dei record.

Il verificatore lavora in streaming, quindi non richiede di caricare tutto il file in memoria.

## 9. Parametri principali

I programmi di ordinamento accettano diversi parametri da riga di comando.

I piu' importanti sono:

- `--chunk-mb`, dimensione dei chunk letti in RAM;
- `--threads`, numero di thread OpenMP;
- `--workers`, numero di worker FastFlow;
- `--tmp-dir`, directory usata per i file temporanei;
- `--merge-fan`, numero massimo di run fuse insieme in una passata;
- `--keep-runs`, utile per conservare i file temporanei e fare debug.

La scelta di `chunk-mb` influenza il numero di run generate. Chunk piu' grandi producono meno run, ma richiedono piu' RAM. Chunk piu' piccoli usano meno memoria, ma aumentano il numero di passate di merge.

La scelta di `merge-fan` influenza il numero di file aperti contemporaneamente e il numero di passate su disco. Un valore troppo basso aumenta le passate; un valore troppo alto puo' aumentare troppo l'uso di memoria e file descriptor.

## 10. Performance evaluation

La valutazione delle prestazioni e' stata organizzata con gli script nella cartella `benchmarks/`. Gli script sono stati costruiti a partire dall'analisi dei moduli del progetto, quindi raccolgono non solo il tempo totale, ma anche i tempi delle due fasi principali stampati dagli eseguibili:

- `Fase 1`: lettura dei chunk, costruzione degli indici, ordinamento e scrittura delle run;
- `Fase 2`: merge K-way locale oppure merge distribuito MPI;
- `Totale`: tempo complessivo della singola esecuzione.

Questa separazione e' importante per interpretare correttamente i risultati: aumentando i thread, la fase di sort dei chunk tende a beneficiare di piu' del parallelismo, mentre la fase di merge e' spesso limitata dalla banda di I/O. Nella versione MPI, inoltre, la fase 2 include trasferimenti di file tra rank e merge ad albero.

### 10.1 Dataset e parametri

I benchmark variano sia il numero di record `N` sia la distribuzione della dimensione dei payload. I due casi principali sono:

- `many_small`: molti record con payload piccolo, utile per stressare confronto delle chiavi, ordinamento degli indici e overhead di scheduling;
- `few_large`: meno record con payload grande, utile per stressare I/O, copie dei payload e merge.

Gli script permettono di modificare i casi senza cambiare il codice:

```bash
BENCHMARK_CASES="many_small:5000000:64 few_large:2048:1048576"
```

Ogni caso ha formato:

```text
nome:numero_record:payload_max_byte
```

Il progetto viene compilato in Release con `PAYLOAD_MAX_BUILD=1048576`, in modo da poter generare anche record con payload fino a 1 MiB. Per evitare risultati dominati solo dalla prima allocazione o da rumore occasionale, ogni configurazione viene ripetuta piu' volte. Con almeno tre ripetizioni, lo script di analisi scarta il trial piu' lento e calcola la media sui rimanenti.

### 10.2 Single-node: OpenMP e FastFlow

Per la singola macchina si varia il numero di thread OpenMP o worker FastFlow:

```bash
THREAD_LIST="1 2 4 8 16 32" TRIALS=5 ./benchmarks/single_node.sh
python3 benchmarks/analyze.py
```

Per ogni implementazione e per ogni dataset si calcolano:

```text
speedup(p)    = T(1) / T(p)
efficiency(p) = speedup(p) / p
```

dove `p` e' il numero di thread o worker. I risultati aggregati sono salvati in:

```text
benchmark_results/run_*/single_node_summary.csv
```

Se `matplotlib` e' disponibile, vengono prodotti anche i grafici in `benchmark_results/run_*/plots/`.

### 10.3 Strong scaling MPI

Per lo strong scaling il dataset resta fisso e si aumentano nodi, processi MPI e thread per processo:

```bash
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 8 16 32" \
BENCHMARK_CASES="manySmall200M:200000000:64" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 \
./benchmarks/mpi_strong.sh
```

Le metriche usate sono:

```text
strong_speedup    = T_base / T(p)
strong_efficiency = strong_speedup / (cores(p) / cores_base)
```

Con piu' di un thread per rank, il merge locale MPI usa il merge OpenMP
multi-pass parallelo; con un solo thread resta il merge locale seriale. La
baseline e' la configurazione con il numero minimo di core totali disponibile
per lo stesso dataset. Il CSV aggregato e':

```text
benchmark_results/run_*/mpi_strong_summary.csv
```

### 10.4 Weak capacity MPI

Per la weak capacity si fissa il tempo e si misura quanta informazione viene
processata. Si ha weak scaling quando, nello stesso intervallo di tempo,
aumentando il numero `p` di processori aumenta la quantita' di lavoro
completata. Lo script non riceve piu' una dimensione dati statica: genera una
sonda interna derivata da `CHUNK_MB`, `MERGE_FAN` e
`WEAK_PROBE_CHUNKS_PER_RANK`, misura il throughput e lo normalizza su 180
secondi:

```bash
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 8 16 32" \
WEAK_PAYLOAD_MAX=64 \
WEAK_TIME_BUDGET_SECONDS=180 \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 \
./benchmarks/mpi_weak.sh
```

Le metriche principali sono:

```text
capacity_gib_per_node = (input_gib / total_s) * 180 / nodes
capacity_total_gib    = (input_gib / total_s) * 180
```

dove `180` e' il budget temporale in secondi. In questo modo, a parita' di
`CHUNK_MB`, `MERGE_FAN` e thread per rank, si legge direttamente quanti GiB ogni
nodo e l'intero job riescono a processare in 3 minuti.

Sul cluster si possono usare direttamente:

```bash
./benchmarks/submit_final_mpi_jobs.sh
```

L'helper sottomette un job MPI separato per ogni coppia `(nodi, thread/rank)`:
strong sotto i 30 minuti e weak con limite di 3 minuti.

## 11. Analisi e modello di costo

Indichiamo con:

- `N` il numero di record;
- `L` la dimensione media del payload;
- `B = N * (12 + L)` la dimensione approssimata del file;
- `C` la dimensione del chunk;
- `R = ceil(B / C)` il numero di run iniziali;
- `P` il numero di processi MPI;
- `t` il numero di thread per processo;
- `F` il parametro `merge-fan`.

### 11.1 Costo single-node

La fase di generazione delle run legge tutto il file, ordina ogni chunk e riscrive le run ordinate. Un modello approssimato e':

```text
T_sort ~= B / BW_read + alpha * N * log(C / (12 + L)) / t_eff + B / BW_write
```

dove `t_eff` e' il numero effettivo di thread utili. Non coincide sempre con `t`, perche' la lettura dei chunk e' prodotta da un solo thread/emitter e perche' il disco puo' diventare il limite prima della CPU.

Il merge locale richiede una o piu' passate. Se `R > F`, il numero di passate e' circa:

```text
q = ceil(log_F(R))
```

Ogni passata legge e riscrive i dati, quindi:

```text
T_merge ~= q * (B / BW_read + B / BW_write) + beta * N * log(F)
```

Questo spiega perche' chunk troppo piccoli peggiorano le prestazioni: aumentano `R`, quindi aumentano il numero di passate e il traffico su disco.

### 11.2 Costo distribuito MPI

Nella versione MPI c'e' una fase iniziale in cui il rank 0 scorre il file per calcolare boundary sicuri tra record:

```text
T_boundary ~= B / BW_scan
```

Questa fase e' sequenziale, ma evita di spezzare record a lunghezza variabile. Dopo il broadcast dei boundary, ogni rank ordina circa `B/P` byte:

```text
T_local ~= T_boundary + T_sort(B/P, t) + T_merge_locale(B/P, F)
```

La fase distribuita usa un merge ad albero. A ogni livello, meta' dei rank invia il proprio file e meta' riceve e fonde. Il traffico totale sulla rete e' circa:

```text
Bytes_rete_totali ~= (B / 2) * log2(P)
```

Sul cammino critico del rank 0, invece, i dati ricevuti sono circa:

```text
Bytes_ricevuti_rank0 ~= B * (1 - 1/P)
```

Il tempo del merge distribuito puo' quindi essere approssimato come:

```text
T_dist_merge ~= sum_l max(T_send_l, T_recv_write_l) + T_2way_merge_l
```

Il `max` rappresenta l'overlap ottenuto con il doppio buffer: mentre un blocco e' in trasferimento MPI, il sender puo' leggere il blocco successivo e il receiver puo' scrivere quello appena ricevuto. L'overlap non elimina pero' il costo del merge successivo tra file locali, che resta vincolato da I/O e dalla banda del filesystem.

### 11.3 Bottleneck principali

I bottleneck osservabili o attesi sono:

- scansione iniziale dei boundary sul rank 0, necessaria per il formato a record variabile;
- lettura sequenziale dei chunk nella fase 1, svolta da un solo produttore;
- banda del disco o del filesystem condiviso, soprattutto con payload grandi;
- merge finale sul rank 0 nella riduzione MPI, che concentra progressivamente una porzione crescente dei dati;
- overhead di scheduling quando i chunk sono troppo piccoli;
- oversubscription se si combinano troppi rank per nodo con troppi thread per rank.

### 11.4 Ottimizzazioni adottate

Le ottimizzazioni principali del progetto sono:

- ordinamento di indici leggeri (`RecordIndex`) invece dello spostamento diretto dei payload;
- bufferizzazione esplicita di lettura/scrittura e uso di `fread_unlocked`/`fwrite_unlocked`;
- limite sui chunk in volo nella versione OpenMP, per controllare il consumo di memoria;
- `merge-fan` configurabile, per bilanciare numero di passate e file aperti;
- parallelismo sui gruppi indipendenti di merge in OpenMP e FastFlow;
- farm FastFlow senza collector, per evitare copie o sincronizzazioni inutili;
- partizionamento MPI su boundary di record validi;
- merge distribuito ad albero invece di inviare tutto direttamente al rank 0;
- invio MPI a blocchi con doppio buffer, per sovrapporre I/O locale e comunicazione.

## 12. Conclusione

Il progetto realizza un ordinatore out-of-core completo per record binari a lunghezza variabile. La soluzione e' basata su un External MergeSort diviso in due fasi: generazione di run ordinate e merge K-way multi-pass.

La parte piu' importante dell'implementazione e' la separazione tra record fisici e indici leggeri. Ordinando solo gli indici, il programma evita di spostare continuamente payload potenzialmente grandi, migliorando l'efficienza.

Le tre versioni mostrano tre modi diversi di parallelizzare lo stesso algoritmo:

- OpenMP usa task su chunk e gruppi di merge.
- FastFlow usa una farm per la fase di sort e `ParallelFor` per il merge.
- MPI + OpenMP distribuisce il file tra rank, ordina localmente e fonde i risultati con un merge ad albero.

Nel complesso, il progetto combina gestione efficiente dell'I/O, parallelismo e attenzione alla correttezza del formato binario, mostrando come adattare un algoritmo classico come MergeSort a dataset piu' grandi della memoria disponibile.
