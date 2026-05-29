# Guida principianti per i benchmark sullo spmcluster

Questa guida usa GitHub per portare il progetto sul cluster e divide i benchmark in job separati. La consegna chiede tre cose:

- variare `N` e la distribuzione dei payload;
- variare i thread OpenMP/FastFlow su singolo nodo e riportare speedup/efficiency;
- fare strong e weak scaling MPI fino a 8 nodi cambiando processi MPI e thread per processo.

L'idea e' prendere dal lavoro del collega la struttura buona, cioe' workload grande, payload diversi, thread sweep e MPI scaling, ma senza una campagna troppo pesante.

Sostituisci `LOGIN` con il tuo username del cluster e `GITHUB_URL` con l'URL del repository.

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

Gli script compilano con CMake in `Release`. Il progetto usa:

```text
-O3
-ffast-math
-march=native
Release/NDEBUG
```

Per questo progetto `-ffast-math` non dovrebbe cambiare l'efficiency in modo significativo: il lavoro e' dominato da ordinamento, merge e I/O, non da calcolo floating point.

## 6. Parametri finali

Usa:

```bash
CHUNK_MB=128
MERGE_FAN=8
VERIFY=0
TRIALS=1
```

Motivazione:

- `50M` record con payload piccolo evita tempi troppo corti;
- `TRIALS=1` tiene i job entro 10-20 minuti;
- `VERIFY=0` evita I/O extra nelle misure;
- `MERGE_FAN=8` evita di collassare subito il merge in un unico gruppo quando le run sono poche.

Fai `VERIFY=1` solo in una run piccola finale di correttezza.

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

## 8. OpenMP single-node

Questo e' il primo benchmark principale: molti record e payload piccolo.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

## 9. FastFlow single-node

Job separata dalla precedente. `APPEND_RESULTS=1` aggiunge le righe FastFlow allo stesso CSV.

```bash
cd ~/spmProject
RUN_OMP=0 \
RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Output:

```text
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
```

## 10. Payload e few-big

Questo benchmark risponde alla parte della consegna su `N` e payload distribution. Include:

- tanti record con payload piccolo;
- meno record con payload medio;
- pochi record con payload grande.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=1 \
BENCHMARK_CASES="payload16:20000000:16 payload512:2000000:512 fewBig2048:500000:2048" \
THREAD_LIST="1 8 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Il caso `fewBig2048` non serve a migliorare per forza l'efficiency. Serve a mostrare cosa succede quando il costo si sposta verso I/O e movimento dati.

## 11. MPI strong scaling

Strong scaling: dataset fisso, nodi crescenti. Con `RANKS_PER_NODE=1`, i processi MPI sono 1, 2, 4, 8. Con `MPI_THREAD_LIST`, cambi anche i thread per processo.

```bash
cd ~/spmProject
RUN_STRONG=1 \
RUN_WEAK=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Output:

```text
benchmark_results/mpi_strong_raw.csv
benchmark_results/mpi_strong_summary.csv
```

## 12. MPI weak scaling

Weak scaling: il lavoro cresce con i nodi. Con `6250000` record per nodo, a 8 nodi arrivi a 50M record totali.

```bash
cd ~/spmProject
RUN_STRONG=0 \
RUN_WEAK=1 \
WEAK_CASES="weakSmall6250k:6250000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Output:

```text
benchmark_results/mpi_weak_raw.csv
benchmark_results/mpi_weak_summary.csv
```

## 13. Controllo correttezza

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

## 14. Rigenerare summary e grafici

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

## 15. Comandi Slurm utili

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

## 16. Portare i risultati su Windows

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

## 17. Cosa riportare nella relazione

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
