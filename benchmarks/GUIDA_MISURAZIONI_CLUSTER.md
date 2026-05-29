# Guida rapida misurazioni

Questa e' la sequenza finale consigliata. Ogni comando lancia un dominio separato, cosi' i job restano leggibili e piu' facili da tenere entro 10-20 minuti.

Usa `50M` come workload principale: dai risultati a 20M i tempi erano troppo corti e il merge dominava troppo presto.

## 1. OpenMP single-node

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## 2. FastFlow single-node

```bash
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## 3. Payload e few-big

```bash
RUN_OMP=1 RUN_FF=1 \
BENCHMARK_CASES="payload16:20000000:16 payload512:2000000:512 fewBig2048:500000:2048" \
THREAD_LIST="1 8 32" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## 4. MPI strong scaling

```bash
RUN_STRONG=1 RUN_WEAK=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
STRONG_NODES="1 2 4 8" RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

## 5. MPI weak scaling

```bash
RUN_STRONG=0 RUN_WEAK=1 \
WEAK_CASES="weakSmall6250k:6250000:64" \
STRONG_NODES="1 2 4 8" RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

## 6. Controllo correttezza

```bash
RUN_OMP=1 RUN_FF=1 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=1 VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## Analisi

```bash
python3 benchmarks/analyze.py --results-dir benchmark_results
```

Usa nella relazione:

```text
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/*.log
```
