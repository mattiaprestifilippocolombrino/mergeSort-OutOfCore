# Guida principianti per i benchmark sullo spmcluster

Questa guida usa GitHub per portare il progetto sul cluster e divide i benchmark in job piccoli. Con la mutua esclusione sul cluster, l'obiettivo e' tenere ogni job intorno a 10-20 minuti, evitando job monolitiche da 30 minuti che rischiano di essere cancellate.

Sostituisci `LOGIN` con il tuo username del cluster e `GITHUB_URL` con l'URL del repository GitHub.

## 1. Login

Da PowerShell:

```powershell
ssh LOGIN@spmcluster.unipi.it
```

Se sei fuori dalla rete UNIPI, attiva prima la VPN.

## 2. Clonare o aggiornare il progetto

Sul cluster:

```bash
cd ~
git clone GITHUB_URL spmProject
cd ~/spmProject
```

Se il progetto esiste gia':

```bash
cd ~/spmProject
git pull
```

## 3. Controlli iniziali

```bash
cd ~/spmProject
which cmake
which g++
which python3
which sbatch
which squeue
which mpicxx || which mpic++ || which mpiCC
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
```

Se manca qualcosa, controlla i moduli:

```bash
module avail
```

Poi carica i moduli disponibili per `cmake`, `gcc` e `mpi`.

## 4. FastFlow

Se non c'e' gia':

```bash
cd ~
git clone https://github.com/fastflow/fastflow.git fastFlow
cd ~/spmProject
```

Nei job FastFlow passa sempre:

```bash
FF_ROOT="$HOME/fastFlow"
```

## 5. Ottimizzazioni di compilazione

Gli script compilano con CMake in `Release`. Il progetto usa gia':

```text
-O3
-ffast-math
-march=native
Release/NDEBUG
```

Quindi le ottimizzazioni principali sono attive. Per controllare la configurazione durante una job, guarda `slurm_*.out`: deve comparire la configurazione `Release`.

## 6. Regole pratiche

Usa:

```bash
CHUNK_MB=128
MERGE_FAN=8
VERIFY=0
```

Perche':

- `CHUNK_MB=128` e' un compromesso tra numero di run temporanee e parallelismo;
- `MERGE_FAN=8` evita che il merge collassi in un unico gruppo quando le run sono poche decine;
- `VERIFY=0` evita I/O extra nelle misure finali.

Usa `VERIFY=1` solo nei test rapidi o nella run finale di controllo correttezza.

Non mischiare troppi domini nella stessa job. I domini consigliati sono:

- OpenMP speedup;
- FastFlow speedup;
- payload variation;
- MPI strong;
- MPI weak;
- controllo correttezza.

## 7. Smoke test

Serve solo a controllare che tutto parta.

```bash
cd ~/spmProject
BENCHMARK_CASES="quick:1000000:64" \
THREAD_LIST="1 2" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

Controlla:

```bash
squeue -u $USER
tail -n 80 slurm_single_*.out
tail -n 80 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
```

## 8. OpenMP speedup

Job singola, dominio OpenMP.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=0 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=2 \
VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Produce righe `impl=omp` in:

```text
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
```

## 9. FastFlow speedup

Job separata. Usa `APPEND_RESULTS=1` per aggiungere le righe FastFlow allo stesso CSV single-node.

```bash
cd ~/spmProject
RUN_OMP=0 \
RUN_FF=1 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=2 \
VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Se FastFlow fallisce, la job non dovrebbe cancellare i risultati OpenMP gia' ottenuti.

## 10. Extra 50M OpenMP

Questa e' una misura extra, separata dal benchmark principale. Serve per avere un caso confrontabile con report che usano 50M record e payload piccolo. Usa `TRIALS=1` per restare dentro una job breve.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=0 \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Non mischiare questa job con payload variation o MPI.

## 11. Extra 50M FastFlow

Job separata dalla precedente, cosi' se FastFlow ha problemi non perdi le misure OpenMP.

```bash
cd ~/spmProject
RUN_OMP=0 \
RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## 12. Payload variation

Serve per la parte della consegna su `N` e distribuzione dei payload. Job separata, piu' piccola.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=1 \
BENCHMARK_CASES="payload16:10000000:16 payload512:2000000:512 payload2048:31250:2048" \
THREAD_LIST="1 8 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=2 \
VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Qui non serve provare tutti i thread: l'obiettivo e' mostrare come cambia il comportamento al crescere del payload.

## 13. MPI strong scaling

Job solo strong scaling.

```bash
cd ~/spmProject
RUN_STRONG=1 \
RUN_WEAK=0 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=2 \
VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Produce:

```text
benchmark_results/mpi_strong_raw.csv
benchmark_results/mpi_strong_summary.csv
```

## 14. MPI weak scaling

Job solo weak scaling.

```bash
cd ~/spmProject
RUN_STRONG=0 \
RUN_WEAK=1 \
WEAK_CASES="weakSmall5M:5000000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=2 \
VERIFY=0 \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Produce:

```text
benchmark_results/mpi_weak_raw.csv
benchmark_results/mpi_weak_summary.csv
```

## 15. Controllo correttezza

Alla fine fai una run piccola con verifica attiva:

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=1 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

Questa run non serve per le curve, serve solo per dire che l'output e' stato verificato.

## 16. Diagnostica merge

Da fare solo se `avg_merge_s` resta alto o quasi costante.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=0 \
BENCHMARK_CASES="mergeDiag20M:20000000:64" \
THREAD_LIST="1 8 32" \
CHUNK_MB=64 \
MERGE_FAN=8 \
MERGE_VERBOSE=1 \
TRIALS=1 \
VERIFY=0 \
sbatch --time=00:15:00 benchmarks/slurm_single_node.sbatch
```

Poi:

```bash
grep -R "\[merge\]" benchmark_results/*.log | head -n 60
```

Se vedi sempre `groups=1`, il merge non sta creando gruppi paralleli in quella configurazione.

## 17. Comandi Slurm utili

```bash
squeue -u $USER
scancel JOBID
tail -n 80 slurm_single_*.out
tail -n 120 slurm_single_*.err
tail -n 80 slurm_mpi_*.out
tail -n 120 slurm_mpi_*.err
```

Seguire un file mentre gira:

```bash
tail -f slurm_single_*.out
```

## 18. Rigenerare summary e grafici

```bash
cd ~/spmProject
python3 benchmarks/analyze.py --results-dir benchmark_results
```

File principali:

```text
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/plots/
benchmark_results/*.log
```

## 19. Portare i risultati su Windows

Sul cluster:

```bash
cd ~/spmProject
tar -czf spm_benchmark_results.tar.gz benchmark_results slurm_single_*.out slurm_single_*.err slurm_mpi_*.out slurm_mpi_*.err
```

Esci dal cluster:

```bash
exit
```

Da PowerShell:

```powershell
mkdir "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/spm_benchmark_results.tar.gz "$env:USERPROFILE\Desktop\spm_benchmark_results\"
```

Oppure scarica direttamente la cartella:

```powershell
scp -r LOGIN@spmcluster.unipi.it:~/spmProject/benchmark_results "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_single_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_single_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_mpi_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_mpi_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
```

## 20. Cosa riportare nella relazione

Single-node:

```text
impl, case, threads, avg_total_s, avg_sort_s, avg_merge_s, speedup, efficiency, generated_runs
```

Strong scaling:

```text
nodes, ranks, threads_per_rank, total_cores, avg_total_s, strong_speedup, strong_efficiency
```

Weak scaling:

```text
nodes, records, records_per_node, threads_per_rank, avg_total_s, weak_efficiency
```

Nei log commenta:

```text
Fase 1
Fase 2
Totale
```
