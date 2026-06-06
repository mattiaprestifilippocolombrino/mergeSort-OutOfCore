
## Preparazione
```bash
cd ~/spmProject
git pull
chmod +x benchmarks/*.sh benchmarks/*.sbatch
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
./benchmarks/setup_scratch.sh
```

## OpenMP single-node

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

## FastFlow single-node

```bash
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
RUN_TIMEOUT_SECONDS=900 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

## Payload distribution

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="mediumPayload8M:8000000:512 largePayload2M:2000000:2048" \
THREAD_LIST="1 8 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

## MPI strong scaling

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

##MPI weak capacity

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

## Correttezza

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## Rigenerare summary e grafici

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
python3 benchmarks/analyze.py --results-dir "$RUN_DIR"
```

