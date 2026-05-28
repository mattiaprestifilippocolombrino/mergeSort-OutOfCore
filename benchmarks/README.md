# Benchmark suite

Questa cartella contiene i benchmark ricreati per il progetto, prendendo spunto da `benchmarks_others` ma usando direttamente i binari e le fasi reali di questa implementazione:

- `generate` per creare input riproducibili;
- `omp_sort` e `ff_sort` per speedup/efficienza single-node;
- `mpi_sort` per strong e weak scaling distribuito;
- parsing dei tempi `Fase 1`, `Fase 2`, `Totale` stampati dai programmi.

## Casi misurati

I casi di default sono volutamente pochi:

- `many_small`: molti record, payload piccolo. Stressa ordinamento degli indici, scheduling dei task e confronto delle chiavi.
- `few_large`: meno record, payload grande. Stressa I/O, copie dei payload e merge.

Sono configurabili senza modificare gli script:

```bash
BENCHMARK_CASES="many_small:5000000:64 few_large:2048:1048576"
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
MERGE_FAN=64
```

Il CSV grezzo e' `benchmark_results/single_node_raw.csv`.
Il CSV aggregato e' `benchmark_results/single_node_summary.csv`.

Metriche:

- `speedup = T_1 / T_p`, calcolato separatamente per OpenMP e FastFlow;
- `efficiency = speedup / p`, con `p = threads` o `workers`.

## MPI strong scaling

Strong scaling: dataset fisso, aumento di nodi/rank/thread.

```bash
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 2 4 8" \
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
MPI_THREAD_LIST="1 2 4 8" \
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
sbatch benchmarks/slurm_single_node.sbatch
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

Gli input e gli output MPI sono messi in `benchmark_data/`, così sono visibili a tutti i nodi se la directory del progetto e' su filesystem condiviso. I temporanei intermedi dei rank restano su `/tmp` o su `$TMPDIR`.

## Note metodologiche

- Gli script compilano in Release con `PAYLOAD_MAX_BUILD=1048576`, così il generatore puo' coprire anche payload grandi.
- Se FastFlow non e' disponibile, i benchmark OpenMP continuano e `ff_sort` viene saltato.
- Con `TRIALS >= 3`, `analyze.py` scarta il trial piu' lento prima di fare la media, come negli script esterni.
- I log completi restano in `benchmark_results/*.log`, utili per discutere i bottleneck di `Fase 1` e `Fase 2`.
- Nel job MPI puoi impostare `RUN_STRONG=0` o `RUN_WEAK=0` per eseguire solo una delle due famiglie di misure.
