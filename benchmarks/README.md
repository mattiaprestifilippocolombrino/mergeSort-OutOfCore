# Benchmark suite

Questa cartella contiene i benchmark ricreati per il progetto, prendendo spunto da `benchmarks_others` ma usando direttamente i binari e le fasi reali di questa implementazione:

- `generate` per creare input riproducibili;
- `omp_sort` e `ff_sort` per speedup/efficienza single-node;
- `mpi_sort` per strong e weak scaling distribuito;
- parsing dei tempi `Fase 1`, `Fase 2`, `Totale` stampati dai programmi.

## Casi misurati

I casi di default negli script shell sono volutamente piccoli, utili per prove rapide:

- `many_small`: molti record, payload piccolo. Stressa ordinamento degli indici, scheduling dei task e confronto delle chiavi.
- `few_large`: meno record, payload grande. Stressa I/O, copie dei payload e merge.

Sono configurabili senza modificare gli script:

```bash
BENCHMARK_CASES="many_small:5000000:64 few_large:2048:1048576"
```

Per una misura piu' adatta alla relazione, usare dataset piu' grandi, ma conviene dividerli in piu' job:

```bash
BENCHMARK_CASES="manySmall50M:50000000:64"
BENCHMARK_CASES="payload16:20000000:16 payload512:5000000:512 payload2048:31250:2048"
```

Ogni voce ha formato:

```text
nome:numero_record:payload_max_byte
```

## Single node

```bash
./benchmarks/single_node.sh
python3 benchmarks/analyze.py --results-dir benchmark_results
```

Variabili utili:

```bash
THREAD_LIST="1 2 4 8 16 32"
TRIALS=5
VERIFY=1
CHUNK_MB=128
MERGE_FAN=8
```

Lo script Slurm single-node usa default piu' adatti al cluster:

```bash
BENCHMARK_CASES="manySmall50M:50000000:64"
THREAD_LIST="1 2 4 8 12 16 20 24 28 32"
TRIALS=3
CHUNK_MB=128
MERGE_FAN=8
```

Il CSV grezzo e' `benchmark_results/single_node_raw.csv`.
Il CSV aggregato e' `benchmark_results/single_node_summary.csv`.
La colonna `generated_runs` indica quante run temporanee sono state prodotte in Fase 1.

Metriche:

- `speedup = T_1 / T_p`, calcolato separatamente per OpenMP e FastFlow;
- `efficiency = speedup / p`, con `p = threads` o `workers`.

## MPI strong scaling

Strong scaling: dataset fisso, aumento di nodi/rank/thread.

```bash
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 2 4 8 16" \
./benchmarks/mpi_strong.sh
```

Metriche:

- `strong_speedup = T_base / T_p`, dove `T_base` e' la configurazione con meno core totali per lo stesso caso;
- `strong_efficiency = speedup / (cores_p / cores_base)`.

Nel default `RANKS_PER_NODE=1`, quindi il numero di processi MPI cresce con i nodi: 1, 2, 4, 8. Questo basta per le curve richieste e mantiene contenuto il numero di run.

## MPI weak scaling

Weak scaling: lavoro proporzionale al numero di nodi.

```bash
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 2 4 8 16" \
WEAK_RECORDS_PER_NODE=1000000 \
WEAK_PAYLOAD_MAX=256 \
./benchmarks/mpi_weak.sh
```

Per lanciare piu' profili weak nella stessa job:

```bash
WEAK_CASES="weak_small:1000000:64 weak_large:2048:1048576" \
./benchmarks/mpi_weak.sh
```

Metriche:

- `weak_efficiency = T_base / T_p`, con baseline al numero minimo di nodi misurato per lo stesso numero di thread per rank.

Anche qui il default mantiene un rank MPI per nodo e varia i thread per rank con `MPI_THREAD_LIST`.

## Esecuzione su SLURM

Per lo spmcluster conviene usare gli script `.sbatch` inclusi. Il job single-node usa `node01`; il job MPI usa `node01-node08`, cioe' gli 8 nodi omogenei del cluster:

```bash
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

Gli input MPI sono messi in `benchmark_data/`, così sono visibili a tutti i nodi se la directory del progetto e' su filesystem condiviso. Gli output finali temporanei e i file intermedi dei rank restano invece in `$TMP_BASE`, sotto `/scratch/$USER/spmRun/$SLURM_JOB_ID` se disponibile, altrimenti sotto `/tmp/$USER/spmRun/$SLURM_JOB_ID`.

Nel job single-node gli input e i temporanei vengono messi nella directory di run del job, sotto `/scratch/$USER/spmRun/$SLURM_JOB_ID` se disponibile, altrimenti sotto `/tmp/$USER/spmRun/$SLURM_JOB_ID`. Nel job MPI gli input restano in `benchmark_data/` per essere visibili a tutti i nodi, mentre i temporanei dei rank vanno nella directory di run locale.

Esempio di benchmark single-node principale per speedup ed efficienza:

```bash
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 12 16 20 24 28 32" \
TRIALS=3 \
VERIFY=1 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

Esempio separato per variare la distribuzione dei payload senza creare una job troppo lunga:

```bash
BENCHMARK_CASES="payload16:20000000:16 payload512:5000000:512 payload2048:31250:2048" \
THREAD_LIST="1 4 8 16 32" \
TRIALS=2 \
VERIFY=1 \
APPEND_RESULTS=1 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

Esempio diagnostico sul merge:

```bash
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 2 4 8 16" \
CHUNK_MB=64 \
MERGE_FAN=16 \
MERGE_VERBOSE=1 \
TRIALS=2 \
VERIFY=1 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

## Note metodologiche

- Rispetto a `benchmarks_others`, questa suite misura le stesse famiglie principali ma evita di mettere troppi casi nella stessa job: una job per lo speedup single-node principale, una job opzionale per payload diversi, una job per MPI strong/weak.
- `CHUNK_MB=128` mantiene basso il numero di run temporanee senza far crescere troppo la memoria per chunk.
- `MERGE_FAN=8` e' scelto per non collassare tutte le run in un unico merge seriale quando i dataset generano poche decine di run. Il vecchio `MERGE_FAN=64` era troppo alto per i casi osservati.
- Gli script compilano in Release con `PAYLOAD_MAX_BUILD=1048576`, così il generatore puo' coprire anche payload grandi.
- Se FastFlow non e' disponibile, i benchmark OpenMP continuano e `ff_sort` viene saltato.
- I CSV raw vengono riscritti a ogni run, così i summary non mescolano misure vecchie e nuove. Se vuoi appendere esplicitamente, imposta `APPEND_RESULTS=1`.
- Con `TRIALS >= 3`, `analyze.py` scarta il trial piu' lento prima di fare la media, come negli script esterni.
- I log completi restano in `benchmark_results/*.log`, utili per discutere i bottleneck di `Fase 1` e `Fase 2`.
- Nel job MPI puoi impostare `RUN_STRONG=0` o `RUN_WEAK=0` per eseguire solo una delle due famiglie di misure.
- Imposta `MERGE_VERBOSE=1` solo per run diagnostiche: aggiunge nei log il numero di run, gruppi e task di merge.
