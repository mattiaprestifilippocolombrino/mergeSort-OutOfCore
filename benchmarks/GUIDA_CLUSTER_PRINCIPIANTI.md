# Guida principianti per i benchmark sullo spmcluster

Questa guida spiega come lanciare i benchmark, dove finiscono i file e quali
colonne guardare. I comandi assumono che il progetto si trovi in `~/spmProject`.

## 1. Entrare nel cluster

Da PowerShell:

```powershell
ssh LOGIN@spmcluster.unipi.it
```

Se sei fuori dalla rete UNIPI, attiva prima la VPN.

## 2. Aggiornare il progetto

Prima volta:

```bash
cd ~
git clone GITHUB_URL spmProject
cd ~/spmProject
```

Volte successive:

```bash
cd ~/spmProject
git pull
chmod +x benchmarks/*.sh benchmarks/*.sbatch
```

Controlli utili:

```bash
which cmake
which g++
which python3
which sbatch
which squeue
which mpicxx || which mpic++ || which mpiCC
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
```

Se usi FastFlow e manca la libreria:

```bash
cd ~
git clone https://github.com/fastflow/fastflow.git fastFlow
cd ~/spmProject
```

Nei job FastFlow imposta:

```bash
FF_ROOT="$HOME/fastFlow"
```

## 3. Dove finiscono i risultati

Ogni job crea una cartella nuova:

```text
benchmark_results/run_<jobid>/
```

Dentro trovi:

```text
single_node_raw.csv
single_node_summary.csv
mpi_strong_raw.csv
mpi_strong_summary.csv
mpi_weak_raw.csv
mpi_weak_summary.csv
logs/
plots/
```

Per puntare sempre all'ultima run:

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
echo "$RUN_DIR"
```

I file grandi temporanei non stanno nei risultati: gli script passano
`--tmp-dir "$TMP_BASE"` ai sorter. I dataset generati per i benchmark stanno in
`DATA_DIR`, che di default e' sotto `TMP_BASE`, oppure sotto `RUN_DIR/data`
negli script Slurm single-node.

## 4. Cosa significano Fase 1 e Fase 2

OpenMP e FastFlow single-node:

- Fase 1: lettura input, sort dei chunk e scrittura delle run ordinate.
- Fase 2: merge delle run.

MPI:

- Fase 1: lavoro locale di ogni rank. Include stripe locale, sort dei chunk e
  merge locale fino a `local_sorted.bin`.
- Fase 2: merge distribuito ad albero tra rank MPI.

Quindi nei CSV MPI `avg_sort_s` indica la fase locale completa, non solo il
tempo di `std::sort`.

## 5. Colonne importanti nei CSV

Single-node:

```text
threads
avg_total_s, avg_sort_s, avg_merge_s
baseline_total_s, speedup, efficiency
baseline_sort_s, sort_speedup, sort_efficiency
baseline_merge_s, merge_speedup, merge_efficiency
```

MPI strong:

```text
nodes, ranks, threads_per_rank, total_cores
avg_total_s, avg_sort_s, avg_merge_s
strong_speedup, strong_efficiency
strong_sort_speedup, strong_sort_efficiency
strong_merge_speedup, strong_merge_efficiency
```

MPI weak:

```text
nodes, records, records_per_node
avg_total_s, avg_sort_s, avg_merge_s
weak_efficiency
weak_sort_efficiency
weak_merge_efficiency
```

Formule:

```text
single speedup = T_1 / T_p
single efficiency = speedup / threads

strong_speedup = T_base / T_p
strong_efficiency = strong_speedup / (nodes / baseline_nodes)

weak_efficiency = T_base / T_p
```

Le varianti `sort_*` e `merge_*` applicano le stesse formule alle singole fasi.

## 6. Smoke test

Serve solo a verificare build, esecuzione e verifier.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="quick:1000000:64" \
THREAD_LIST="1 2" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

Controlla:

```bash
squeue -u "$USER"
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## 7. OpenMP single-node

Benchmark principale per speedup ed efficiency su un nodo.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Controlla:

```bash
tail -n 120 slurm_single_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
tail -n 80 "$RUN_DIR"/logs/omp_*.log
```

Nel report commenta soprattutto la differenza tra `sort_efficiency` e
`merge_efficiency`: il sort tende a scalare meglio, il merge e l'I/O limitano
il totale.

## 8. FastFlow single-node

Lancialo separato da OpenMP.

```bash
cd ~/spmProject
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
RUN_TIMEOUT_SECONDS=180 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Controlla:

```bash
tail -n 120 slurm_single_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
tail -n 80 "$RUN_DIR"/logs/ff_*.log
```

Se trovi `timeout`, `pthread_create` o errori di worker FastFlow, conserva il
log e segnala la run come non valida.

## 9. Payload distribution

Serve a confrontare molti record piccoli con meno record e payload piu' grande.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="mediumPayload8M:8000000:512 largePayload2M:2000000:2048" \
THREAD_LIST="1 8 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Controlla:

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## 10. MPI strong scaling

Dataset fisso, nodi crescenti. Lo script copia l'input su `/tmp` locale dei
nodi prima della misura, quindi la copia non entra in Fase 1/Fase 2/Totale.

```bash
cd ~/spmProject
RUN_STRONG=1 RUN_WEAK=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Controlla:

```bash
squeue -u "$USER"
tail -f slurm_mpi_*.out
tail -n 160 slurm_mpi_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/mpi_strong_summary.csv"
tail -n 80 "$RUN_DIR"/logs/mpi_strong_*.log
```

Nel report collega questa parte ad Amdahl: comunicazione, I/O, merge locale e
merge distribuito limitano lo speedup.

## 11. MPI weak scaling

Il lavoro cresce con i nodi: `6.25M` record per nodo.

```bash
cd ~/spmProject
RUN_STRONG=0 RUN_WEAK=1 \
WEAK_CASES="weakSmall6250k:6250000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Controlla:

```bash
tail -n 160 slurm_mpi_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/mpi_weak_summary.csv"
```

Nel report collega questa parte a Gustafson e alla weak efficiency.

## 12. Correttezza finale

Fai una run piccola con `VERIFY=1`, separata dalle misure finali.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## 13. Rigenerare summary e grafici

```bash
cd ~/spmProject
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
python3 benchmarks/analyze.py --results-dir "$RUN_DIR"
```

I grafici sono in:

```text
$RUN_DIR/plots/
```

## 14. Scaricare i risultati su Windows

Sul cluster:

```bash
cd ~/spmProject
tar -czf spm_benchmark_results.tar.gz benchmark_results slurm_single_*.out slurm_single_*.err slurm_mpi_*.out slurm_mpi_*.err slurm_tune_single_*.out slurm_tune_single_*.err
```

Poi esci:

```bash
exit
```

Da PowerShell:

```powershell
mkdir "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/spm_benchmark_results.tar.gz "$env:USERPROFILE\Desktop\spm_benchmark_results\"
```

## 15. Cosa scrivere nella relazione

Commenta:

- speedup ed efficiency totali;
- speedup/efficiency della Fase 1 e della Fase 2;
- perché il merge scala meno del sort;
- payload piccoli contro payload grandi;
- Amdahl per strong scaling;
- Gustafson per weak scaling;
- eventuali run FastFlow non valide, con riferimento ai log.

