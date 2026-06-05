
## MPI strong scaling

Dataset fisso, nodi crescenti. Lo script copia l'input su `/scratch` locale dei
nodi usati prima del sorter; questa copia non entra nei tempi.

Job finali strong, uno per punto della curva:

```bash
for n in 1 2 4 8; do
  for t in 1 4 8 16 32; do
    RUN_STRONG=1 RUN_WEAK=0 \
    BENCHMARK_CASES="manySmall200M:200000000:64" \
    STRONG_NODES="$n" \
    RANKS_PER_NODE=1 \
    MPI_THREAD_LIST="$t" \
    PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
    TRIALS=1 VERIFY=0 \
    sbatch --nodes="$n" --time=00:29:00 benchmarks/slurm_mpi_scaling.sbatch
  done
done
```

In alternativa, sottometti strong e weak insieme con l'helper finale:

```bash
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 8 16 32" \
./benchmarks/submit_final_mpi_jobs.sh
```

L'helper usa job separati per ogni coppia `(nodi, thread/rank)`: `00:29:00`
per MPI strong e `00:03:00` per MPI weak, mantenendo identici `CHUNK_MB` e
`MERGE_FAN`.

Controllo:

```bash
squeue -u "$USER"
tail -f slurm_mpi_*.out
tail -n 160 slurm_mpi_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/mpi_strong_summary.csv"
ls -lh "$RUN_DIR/logs"
```

Nel report interpreta questa parte con Amdahl: I/O, merge locale, merge
distribuito e comunicazione limitano lo speedup. Con `threads/rank > 1` il
merge locale usa OpenMP multi-pass parallelo e nel CSV appare come
`mpi_local_omp_multipass`.

## MPI weak capacity

La weak finale non passa piu' una dimensione statica del dataset. Per ogni
coppia `(nodi, thread/rank)` lo script genera una sonda interna derivata da
`CHUNK_MB`, `MERGE_FAN` e `WEAK_PROBE_CHUNKS_PER_RANK`, misura il throughput e
lo normalizza su `WEAK_TIME_BUDGET_SECONDS=180`. Il risultato da usare nel
report e' quanti GiB vengono processati in 3 minuti per nodo e in totale.

Job finali weak, uno per coppia `(nodi, thread/rank)`:

```bash
for n in 1 2 4 8; do
  for t in 1 4 8 16 32; do
    RUN_STRONG=0 RUN_WEAK=1 \
    STRONG_NODES="$n" \
    RANKS_PER_NODE=1 \
    MPI_THREAD_LIST="$t" \
    PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
    WEAK_TIME_BUDGET_SECONDS=180 \
    TRIALS=1 VERIFY=0 \
    sbatch --nodes="$n" --time=00:03:00 benchmarks/slurm_mpi_scaling.sbatch
  done
done
```

Controllo:

```bash
tail -n 160 slurm_mpi_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/mpi_weak_summary.csv"
```

Nel report interpreta questa parte con Gustafson e con la weak efficiency.

## Correttezza

La verifica va tenuta fuori dai benchmark finali e fatta su una run piccola.

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## Varianti legacy

Le vecchie varianti pipeline e flat non sono piu' parte della campagna attiva.
Restano archiviate nelle cartelle `legacy` per consultazione storica.

## Rigenerare summary e grafici

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
python3 benchmarks/analyze.py --results-dir "$RUN_DIR"
```

## Tuning opzionale (Grid Search)

Per trovare la migliore combinazione di `CHUNK_MB` e `MERGE_FAN`, usa la guida
dedicata:

```text
benchmarks/GUIDA_TUNING_OPENMP.md
```

Il tuning usa solo OpenMP e propone piu' job brevi invece di una singola grid
search lunga.
