# Guida rapida misurazioni cluster

Questa e' la sequenza finale consigliata. Non rifare il tuning lungo: dai dati raccolti la configurazione da usare e':

```bash
CHUNK_MB=64
MERGE_FAN=8
PAYLOAD_MAX_BUILD=4096
TRIALS=1
VERIFY=0
```

Con il merge flat nuovo `MERGE_FAN` resta solo per compatibilita' e per eventuali run legacy: nella campagna finale la variabile da tenere sotto controllo e' `CHUNK_MB`.

## 0. Preparazione

```bash
cd ~/spmProject
git pull
chmod +x benchmarks/*.sh benchmarks/*.sbatch
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
```

## 1. OpenMP single-node

Benchmark principale per speedup ed efficiency su singolo nodo. Include `threads=1`, quindi:

```text
speedup = Tseq / Tpar
efficiency = speedup / threads
```

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Da guardare:

```bash
squeue -u $USER
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
```

## 2. FastFlow single-node

Eseguilo separato da OpenMP. Se FastFlow fallisce o va in timeout, conserva i log e riportalo come limite sperimentale.

```bash
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
RUN_TIMEOUT_SECONDS=180 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Da guardare:

```bash
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
ls -lh benchmark_results/ff_*.log
tail -n 80 benchmark_results/ff_*.log
```

## 3. Payload distribution

Serve a soddisfare la richiesta di variare `N` e payload.

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="mediumPayload8M:8000000:512 largePayload2M:2000000:2048" \
THREAD_LIST="1 8 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Da guardare:

```bash
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
```

## 4. MPI strong scaling

Dataset fisso, nodi crescenti. Interpreta i risultati con Amdahl: merge, I/O e comunicazione limitano lo speedup.
Lo script genera il dataset in `benchmark_data`, poi lo copia automaticamente
su `/tmp` dei nodi usati dalla run MPI prima di lanciare il sorter. Questa
copia non entra nei tempi `Fase 1/Fase 2/Totale`.

```bash
RUN_STRONG=1 RUN_WEAK=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
STRONG_NODES="1 2 4 8" RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Da guardare:

```bash
squeue -u $USER
tail -f slurm_mpi_*.out
tail -n 160 slurm_mpi_*.err
cat benchmark_results/mpi_strong_summary.csv
```

## 5. MPI weak scaling

Il lavoro cresce con i nodi: `6.25M` record per nodo, quindi `50M` record a 8 nodi. Interpreta i risultati con Gustafson e con la weak efficiency.
Anche qui ogni input viene copiato su `/tmp` locale dei nodi prima della misura,
cosi' i rank non leggono le stripe da NFS.

```bash
RUN_STRONG=0 RUN_WEAK=1 \
WEAK_CASES="weakSmall6250k:6250000:64" \
STRONG_NODES="1 2 4 8" RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Da guardare:

```bash
tail -n 160 slurm_mpi_*.err
cat benchmark_results/mpi_weak_summary.csv
```

## 6. Correttezza

Run piccola con verifica attiva, separata dalle misure.

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## 7. Rigenerare summary

```bash
python3 benchmarks/analyze.py --results-dir benchmark_results
```

File principali:

```text
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/*.log
slurm_single_*.out / .err
slurm_mpi_*.out / .err
```

Il tuning e' opzionale. Se vuoi rifarlo:

```bash
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 32" \
CHUNK_MB_LIST="32 64 128" \
PAYLOAD_MAX_BUILD=4096 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:10:00 benchmarks/slurm_tune_single_node.sbatch
```
