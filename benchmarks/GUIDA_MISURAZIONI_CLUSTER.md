# Guida rapida misurazioni

Questa e' la sequenza consigliata dopo i risultati su `manySmall50M`.

Il dato importante e' che il sort scala, mentre il merge resta dominante:

```text
threads 1  -> sort 14.7s, merge 19.8s, totale 34.5s
threads 32 -> sort  4.7s, merge 14.8s, totale 19.5s
```

Quindi prima della campagna finale si fa un tuning leggero di `CHUNK_MB` e `MERGE_FAN`. Dopo si usano i parametri migliori per OpenMP, FastFlow e MPI.

## 0. Tuning OpenMP di chunk/fan

Questo job prova poche combinazioni, solo OpenMP, solo thread `8` e `32`.

```bash
cd ~/spmProject
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="8 32" \
CHUNK_MB_LIST="64 128 256" \
MERGE_FAN_LIST="4 8 16" \
TRIALS=1 \
VERIFY=0 \
sbatch --time=00:15:00 benchmarks/slurm_tune_single_node.sbatch
```

Dopo il lancio guarda:

```bash
squeue -u $USER
tail -f slurm_tune_single_*.out
tail -n 120 slurm_tune_single_*.err
ls -lh benchmark_results/single_node_tuning_*.csv
```

Quando finisce:

```bash
python3 benchmarks/analyze.py --results-dir benchmark_results
cat benchmark_results/single_node_tuning_summary.csv
```

Scegli la coppia `chunk_mb,merge_fan` con `avg_total_s` piu' basso a `threads=32`. Se due configurazioni sono simili, preferisci quella che va bene anche a `threads=8`.

Se non vuoi ancora decidere, usa il default attuale:

```bash
CHUNK_MB=64
MERGE_FAN=4
```

## 1. OpenMP single-node finale

Sostituisci `CHUNK_MB` e `MERGE_FAN` con i valori scelti dal tuning.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
CHUNK_MB=64 MERGE_FAN=4 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Dopo il lancio guarda:

```bash
squeue -u $USER
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
```

## 2. FastFlow single-node finale

FastFlow e' in un job separato. Il timeout per singola run evita che una configurazione bloccata consumi tutto il job.

```bash
cd ~/spmProject
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
CHUNK_MB=64 MERGE_FAN=4 \
TRIALS=1 VERIFY=0 \
RUN_TIMEOUT_SECONDS=180 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Se FastFlow fallisce su `50M`, conserva i log e riportalo come limite sperimentale. Non rilanciare job enormi alla cieca.

Dopo il lancio guarda:

```bash
squeue -u $USER
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
ls -lh benchmark_results/ff_*.log
tail -n 80 benchmark_results/ff_*.log
```

## 3. Payload distribution

Serve per la parte della consegna su `N` e payload. Usa thread ridotti per non creare un job gigante.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="payload16:20000000:16 payload512:2000000:512 fewBig2048:500000:2048" \
THREAD_LIST="1 8 32" \
CHUNK_MB=64 MERGE_FAN=4 \
TRIALS=1 VERIFY=0 \
APPEND_RESULTS=1 \
sbatch --time=00:15:00 benchmarks/slurm_single_node.sbatch
```

Se FastFlow e' stabile, puoi fare lo stesso job con `RUN_OMP=0 RUN_FF=1`.

Dopo il lancio guarda:

```bash
squeue -u $USER
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
```

## 4. MPI strong scaling

Dataset fisso, nodi crescenti.

```bash
cd ~/spmProject
RUN_STRONG=1 RUN_WEAK=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
STRONG_NODES="1 2 4 8" RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=64 MERGE_FAN=4 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Dopo il lancio guarda:

```bash
squeue -u $USER
tail -f slurm_mpi_*.out
tail -n 160 slurm_mpi_*.err
cat benchmark_results/mpi_strong_summary.csv
```

## 5. MPI weak scaling

Il lavoro cresce con i nodi: `6.25M` record per nodo, quindi `50M` record totali a 8 nodi.

```bash
cd ~/spmProject
RUN_STRONG=0 RUN_WEAK=1 \
WEAK_CASES="weakSmall6250k:6250000:64" \
STRONG_NODES="1 2 4 8" RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=64 MERGE_FAN=4 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Dopo il lancio guarda:

```bash
squeue -u $USER
tail -f slurm_mpi_*.out
tail -n 160 slurm_mpi_*.err
cat benchmark_results/mpi_weak_summary.csv
```

## 6. Controllo correttezza

Una run piccola con verifica attiva, separata dalle misure.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
CHUNK_MB=64 MERGE_FAN=4 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

Dopo il lancio guarda:

```bash
squeue -u $USER
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
tail -n 80 benchmark_results/omp_check_*.log
```

## Analisi

```bash
cd ~/spmProject
python3 benchmarks/analyze.py --results-dir benchmark_results
```

File da usare nella relazione:

```text
benchmark_results/single_node_tuning_summary.csv
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/*.log
```

## Cosa guardare subito dopo ogni `sbatch`

1. `squeue -u $USER`: capisci se il job e' in coda, in esecuzione o finito.
2. `slurm_*.out`: e' il log generale del job; contiene parametri, build, dataset e avanzamento.
3. `slurm_*.err`: contiene errori Slurm, CMake, MPI, timeout o crash.
4. `benchmark_results/*_raw.csv`: contiene le singole run completate.
5. `benchmark_results/*_summary.csv`: contiene medie, speedup ed efficiency dopo `analyze.py`.
6. `benchmark_results/*.log`: contiene il dettaglio di ogni esecuzione, soprattutto `Fase 1`, `Fase 2`, `Totale`.
