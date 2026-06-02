# Guida rapida misurazioni cluster

Questa guida contiene la sequenza consigliata per produrre i risultati finali.
Ogni job crea una cartella dedicata in `benchmark_results/run_<jobid>/`, con
CSV, summary, grafici e log separati.

## Parametri consigliati

```bash
CHUNK_MB=64
MERGE_FAN=8
PAYLOAD_MAX_BUILD=4096
TRIALS=1
VERIFY=0
```

Nei CSV verrà indicata la `local_merge_impl` o `merge_impl` come **pipeline** (che rappresenta il nuovo approccio a doppio buffer con I/O asincrono). Il numero `MERGE_FAN` per via del pipeline/flat attuale viene stampato come `non usato`.

Tutti i log si trovano nella cartella `results/logs`. Qui è possibile leggere la scomposizione per "Fase 1 (sort)" e "Fase 2 (merge)".

## Preparazione

```bash
cd ~/spmProject
git pull
chmod +x benchmarks/*.sh benchmarks/*.sbatch
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
```

## Layout risultati

Ogni job scrive in una cartella propria:

```text
benchmark_results/
  run_<jobid>/
    single_node_raw.csv
    single_node_summary.csv
    mpi_strong_raw.csv
    mpi_strong_summary.csv
    mpi_weak_raw.csv
    mpi_weak_summary.csv
    logs/
      *.log
    plots/
      *.png
```

Per trovare l'ultima run:

```bash
ls -td benchmark_results/run_* | head -n 1
```

Esempio:

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## Significato delle fasi

Single-node OpenMP/FastFlow:

- `avg_sort_s`: Fase 1, sort dei chunk e scrittura delle run.
- `avg_merge_s`: Fase 2, merge delle run.
- `avg_total_s`: tempo totale del sorter.

MPI:

- `avg_sort_s`: Fase 1 locale, cioe' distribuzione stripe, sort locale dei chunk
  e merge locale dentro ogni rank fino a `local_sorted.bin`.
- `avg_merge_s`: Fase 2 distribuita, cioe' merge ad albero tra rank MPI.
- `avg_total_s`: tempo totale visto da rank 0.

Quindi in MPI il nome storico `sort_s` va letto come "fase locale", non come
solo tempo di `std::sort`.

## Metriche nei summary

Single-node:

```text
speedup = baseline_total_s / avg_total_s
efficiency = speedup / threads
sort_speedup = baseline_sort_s / avg_sort_s
sort_efficiency = sort_speedup / threads
merge_speedup = baseline_merge_s / avg_merge_s
merge_efficiency = merge_speedup / threads
```

MPI strong:

```text
strong_speedup = T_base / T_p
strong_efficiency = strong_speedup / (nodes / baseline_nodes)
strong_sort_speedup, strong_sort_efficiency
strong_merge_speedup, strong_merge_efficiency
```

MPI weak:

```text
weak_efficiency = T_base / T_p
weak_sort_efficiency = sort_base / sort_p
weak_merge_efficiency = merge_base / merge_p
```

I grafici in `plots/` mostrano insieme total, phase 1 e phase 2.

## OpenMP single-node

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Controllo:

```bash
squeue -u "$USER"
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## FastFlow single-node

Eseguilo separato da OpenMP, cosi' eventuali problemi FastFlow non sporcano la
campagna OpenMP.

```bash
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
RUN_TIMEOUT_SECONDS=180 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Controllo:

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
tail -n 80 "$RUN_DIR"/logs/ff_*.log
```

Se compaiono `timeout`, `pthread_create` o errori di spawning worker, quella run
non e' valida: conserva il log e commentalo.

## Payload distribution

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="mediumPayload8M:8000000:512 largePayload2M:2000000:2048" \
THREAD_LIST="1 8 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Controllo:

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## MPI strong scaling

Dataset fisso, nodi crescenti. Lo script copia l'input su `/tmp` locale dei
nodi usati prima del sorter; questa copia non entra nei tempi.

```bash
RUN_STRONG=1 RUN_WEAK=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

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
distribuito e comunicazione limitano lo speedup.

## MPI weak scaling

Il lavoro cresce con i nodi: `6.25M` record per nodo, quindi `50M` record a 8
nodi.

```bash
RUN_STRONG=0 RUN_WEAK=1 \
WEAK_CASES="weakSmall6250k:6250000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
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

## Rigenerare summary e grafici

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
python3 benchmarks/analyze.py --results-dir "$RUN_DIR"
```

## Tuning opzionale

```bash
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 32" \
CHUNK_MB_LIST="32 64 128" \
PAYLOAD_MAX_BUILD=4096 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:10:00 benchmarks/slurm_tune_single_node.sbatch
```

