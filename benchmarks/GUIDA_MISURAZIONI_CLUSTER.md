# Guida rapida misurazioni

La guida completa per principianti e' in:

```text
benchmarks/GUIDA_CLUSTER_PRINCIPIANTI.md
```

Comandi principali sul cluster, dentro `~/spmProject`.

## Single-node principale

```bash
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 12 16 20 24 28 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=3 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

## Payload diversi

```bash
BENCHMARK_CASES="payload16:20000000:16 payload512:5000000:512 payload2048:31250:2048" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=2 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

## MPI strong e weak

```bash
BENCHMARK_CASES="manySmall50M:50000000:64" \
WEAK_CASES="weakSmall10M:10000000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 2 4 8 16" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=3 \
VERIFY=1 \
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

## Analisi

```bash
python3 benchmarks/analyze.py --results-dir benchmark_results
```

File da usare:

```text
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/plots/
benchmark_results/*.log
```
