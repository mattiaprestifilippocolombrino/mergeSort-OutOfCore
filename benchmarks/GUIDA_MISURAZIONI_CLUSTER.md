# Guida rapida misurazioni

La guida completa e' in `benchmarks/GUIDA_CLUSTER_PRINCIPIANTI.md`.

Ogni comando lancia un dominio separato e dovrebbe restare intorno a 10-20 minuti.

## OpenMP

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=2 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## FastFlow

```bash
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=2 VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## Extra 50M OpenMP

Misura extra, separata dal benchmark principale. Serve per confrontarsi con report che usano 50M record. Non mischiarla con payload o MPI.

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## Extra 50M FastFlow

```bash
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## Payload

```bash
RUN_OMP=1 RUN_FF=1 \
BENCHMARK_CASES="payload16:10000000:16 payload512:2000000:512 payload2048:31250:2048" \
THREAD_LIST="1 8 32" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=2 VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## MPI Strong

```bash
RUN_STRONG=1 RUN_WEAK=0 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
STRONG_NODES="1 2 4 8" RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=2 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

## MPI Weak

```bash
RUN_STRONG=0 RUN_WEAK=1 \
WEAK_CASES="weakSmall5M:5000000:64" \
STRONG_NODES="1 2 4 8" RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=128 MERGE_FAN=8 \
TRIALS=2 VERIFY=0 \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
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
