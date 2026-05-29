# Guida principianti per i benchmark sullo spmcluster

Questa guida spiega come eseguire le misure finali sul cluster partendo dal progetto su GitHub.

La consegna chiede:

- variare il numero di record `N`;
- variare la distribuzione del payload;
- variare i thread OpenMP/FastFlow su singolo nodo;
- riportare speedup ed efficiency;
- fare strong e weak scaling MPI fino a 8 nodi, cambiando processi MPI e thread per processo.

I job sono divisi per dominio. Prima si fa un tuning leggero di `CHUNK_MB` e `MERGE_FAN`, poi si lanciano le misure finali.

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

Se manca qualcosa, controlla i moduli disponibili:

```bash
module avail
```

Poi carica i moduli necessari per `cmake`, `gcc` e `mpi`, se il cluster li richiede.

## 4. FastFlow

Se vuoi misurare FastFlow:

```bash
cd ~
git clone https://github.com/fastflow/fastflow.git fastFlow
cd ~/spmProject
```

Nei job FastFlow passa sempre:

```bash
FF_ROOT="$HOME/fastFlow"
```

Se FastFlow fallisce o supera il timeout, conserva i log. Non rilanciare subito job grandi: prima fai funzionare una run piccola.

## 5. Ottimizzazioni di compilazione

Gli script compilano con CMake in `Release`. Il progetto usa:

```text
-O3
-ffast-math
-march=native
Release/NDEBUG
```

Per questo progetto `-ffast-math` non dovrebbe cambiare l'efficiency in modo rilevante, perche' il costo principale e' ordinamento, merge e I/O, non calcolo floating point.

## 6. Regola pratica sui job

Non lanciare un unico job enorme. Usa questa sequenza:

```text
1. smoke test
2. tuning OpenMP chunk/fan
3. OpenMP single-node finale
4. FastFlow single-node finale
5. payload distribution
6. MPI strong scaling
7. MPI weak scaling
8. controllo correttezza
```

Ogni job deve stare idealmente entro 10-20 minuti.

## 7. Smoke test

Serve solo a controllare che tutto parta.

```bash
cd ~/spmProject
BENCHMARK_CASES="quick:1000000:64" \
THREAD_LIST="1 2" \
CHUNK_MB=64 \
MERGE_FAN=4 \
TRIALS=1 \
VERIFY=1 \
RUN_OMP=1 \
RUN_FF=0 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

Controlla:

```bash
squeue -u $USER
tail -n 80 slurm_single_*.out
tail -n 80 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
```

Se il job non compare piu' in `squeue`, e' finito. A quel punto guarda prima `slurm_single_*.err`: se e' vuoto o quasi vuoto, passa al CSV.

## 8. Tuning OpenMP di `CHUNK_MB` e `MERGE_FAN`

I risultati su `manySmall50M` hanno mostrato che il merge e' il collo di bottiglia. Quindi prima della campagna finale proviamo poche combinazioni:

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

Quando finisce:

```bash
cd ~/spmProject
python3 benchmarks/analyze.py --results-dir benchmark_results
cat benchmark_results/single_node_tuning_summary.csv
```

File da guardare per il tuning:

```text
slurm_tune_single_JOBID.out
slurm_tune_single_JOBID.err
benchmark_results/single_node_tuning_raw.csv
benchmark_results/single_node_tuning_summary.csv
benchmark_results/omp_manySmall50M_*.log
```

Nel file `single_node_tuning_summary.csv` guarda soprattutto `chunk_mb`, `merge_fan`, `generated_runs`, `avg_total_s`, `avg_sort_s`, `avg_merge_s`.

Come scegliere:

- guarda le righe con `threads=32`;
- scegli la coppia `chunk_mb,merge_fan` con `avg_total_s` piu' basso;
- se due coppie sono simili, preferisci quella che resta buona anche a `threads=8`;
- controlla anche `generated_runs`: poche run significano poco parallelismo nel merge.

Default se non hai ancora scelto:

```bash
CHUNK_MB=64
MERGE_FAN=4
```

Nelle sezioni successive sostituisci questi valori con quelli scelti dal tuning.

## 9. OpenMP single-node finale

Questo e' il benchmark principale per speedup ed efficiency su singolo nodo.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
CHUNK_MB=64 \
MERGE_FAN=4 \
TRIALS=1 \
VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Output:

```text
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
```

File da guardare dopo OpenMP:

```text
slurm_single_JOBID.out
slurm_single_JOBID.err
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
benchmark_results/omp_manySmall50M_*.log
```

Nel summary guarda `threads`, `avg_total_s`, `avg_sort_s`, `avg_merge_s`, `speedup`, `efficiency`.

## 10. FastFlow single-node finale

FastFlow e' separato da OpenMP. Questo evita che un problema FastFlow rovini i risultati OpenMP.

```bash
cd ~/spmProject
RUN_OMP=0 \
RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
CHUNK_MB=64 \
MERGE_FAN=4 \
TRIALS=1 \
VERIFY=0 \
RUN_TIMEOUT_SECONDS=180 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Se alcune righe FastFlow falliscono, guarda i log:

```bash
ls -lh benchmark_results/ff_*.log
tail -n 80 benchmark_results/ff_*.log
```

Le righe riuscite finiscono comunque nel CSV. Le righe fallite vanno citate come limite sperimentale, non nascoste.

File da guardare dopo FastFlow:

```text
slurm_single_JOBID.out
slurm_single_JOBID.err
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
benchmark_results/ff_manySmall50M_*.log
```

Se trovi `timeout`, `pthread_create`, `spawning worker thread` o `time limit`, quella run non e' valida: tieni il log per discuterla.

## 11. Payload distribution

Questo benchmark risponde alla parte della consegna su `N` e payload distribution. Usa meno thread per restare leggero.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=0 \
BENCHMARK_CASES="payload16:20000000:16 payload512:2000000:512 fewBig2048:500000:2048" \
THREAD_LIST="1 8 32" \
CHUNK_MB=64 \
MERGE_FAN=4 \
TRIALS=1 \
VERIFY=0 \
APPEND_RESULTS=1 \
sbatch --time=00:15:00 benchmarks/slurm_single_node.sbatch
```

Se FastFlow e' stabile, puoi aggiungere la stessa misura:

```bash
cd ~/spmProject
RUN_OMP=0 \
RUN_FF=1 \
BENCHMARK_CASES="payload16:20000000:16 payload512:2000000:512 fewBig2048:500000:2048" \
THREAD_LIST="1 8 32" \
CHUNK_MB=64 \
MERGE_FAN=4 \
TRIALS=1 \
VERIFY=0 \
RUN_TIMEOUT_SECONDS=180 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:15:00 benchmarks/slurm_single_node.sbatch
```

`fewBig2048` non serve a ottenere efficiency alta. Serve a mostrare cosa succede quando il costo si sposta verso I/O e movimento dati.

File da guardare dopo payload distribution:

```text
slurm_single_JOBID.out
slurm_single_JOBID.err
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
benchmark_results/omp_payload16_*.log
benchmark_results/omp_payload512_*.log
benchmark_results/omp_fewBig2048_*.log
```

Nel summary confronta i casi `payload16`, `payload512`, `fewBig2048` a pari thread.

## 12. MPI strong scaling

Strong scaling: dataset fisso, nodi crescenti. Con `RANKS_PER_NODE=1`, i processi MPI sono 1, 2, 4, 8.

```bash
cd ~/spmProject
RUN_STRONG=1 \
RUN_WEAK=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=64 \
MERGE_FAN=4 \
TRIALS=1 \
VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Output:

```text
benchmark_results/mpi_strong_raw.csv
benchmark_results/mpi_strong_summary.csv
```

File da guardare dopo MPI strong:

```text
slurm_mpi_JOBID.out
slurm_mpi_JOBID.err
benchmark_results/mpi_strong_raw.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_strong_manySmall50M_*.log
```

Nel summary guarda `nodes`, `ranks`, `threads_per_rank`, `total_cores`, `avg_total_s`, `strong_speedup`, `strong_efficiency`.

## 13. MPI weak scaling

Weak scaling: il lavoro cresce con i nodi. Con `6250000` record per nodo, a 8 nodi arrivi a 50M record totali.

```bash
cd ~/spmProject
RUN_STRONG=0 \
RUN_WEAK=1 \
WEAK_CASES="weakSmall6250k:6250000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 16" \
CHUNK_MB=64 \
MERGE_FAN=4 \
TRIALS=1 \
VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Output:

```text
benchmark_results/mpi_weak_raw.csv
benchmark_results/mpi_weak_summary.csv
```

File da guardare dopo MPI weak:

```text
slurm_mpi_JOBID.out
slurm_mpi_JOBID.err
benchmark_results/mpi_weak_raw.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/mpi_weak_weakSmall6250k_*.log
```

Nel summary guarda `nodes`, `records`, `records_per_node`, `avg_total_s`, `weak_efficiency`.

## 14. Controllo correttezza

Alla fine fai una run piccola con verifica attiva.

```bash
cd ~/spmProject
RUN_OMP=1 \
RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
CHUNK_MB=64 \
MERGE_FAN=4 \
TRIALS=1 \
VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## 15. Rigenerare summary e grafici

```bash
cd ~/spmProject
python3 benchmarks/analyze.py --results-dir benchmark_results
```

File principali:

```text
benchmark_results/single_node_tuning_summary.csv
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/plots/
benchmark_results/*.log
```

## 16. Comandi Slurm utili

```bash
squeue -u $USER
scancel JOBID
tail -n 80 slurm_tune_single_*.out
tail -n 120 slurm_tune_single_*.err
tail -n 80 slurm_single_*.out
tail -n 120 slurm_single_*.err
tail -n 80 slurm_mpi_*.out
tail -n 120 slurm_mpi_*.err
```

Seguire un file mentre gira:

```bash
tail -f slurm_single_*.out
```

## 16.1 Ordine giusto per controllare un job

Dopo ogni `sbatch`, usa sempre questo ordine:

```bash
squeue -u $USER
```

Se il job e' ancora in esecuzione:

```bash
tail -f slurm_single_*.out
```

oppure, per MPI:

```bash
tail -f slurm_mpi_*.out
```

Se il job e' finito:

```bash
tail -n 120 slurm_single_*.err
tail -n 120 slurm_mpi_*.err
python3 benchmarks/analyze.py --results-dir benchmark_results
```

Poi apri il summary giusto:

```bash
cat benchmark_results/single_node_tuning_summary.csv
cat benchmark_results/single_node_summary.csv
cat benchmark_results/mpi_strong_summary.csv
cat benchmark_results/mpi_weak_summary.csv
```

Infine guarda i log specifici se una riga manca dal CSV o sembra strana:

```bash
ls -lh benchmark_results/*.log
tail -n 80 benchmark_results/NOME_LOG.log
```

## 17. Portare i risultati su Windows

Sul cluster:

```bash
cd ~/spmProject
tar -czf spm_benchmark_results.tar.gz benchmark_results slurm_tune_single_*.out slurm_tune_single_*.err slurm_single_*.out slurm_single_*.err slurm_mpi_*.out slurm_mpi_*.err
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
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_tune_single_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_tune_single_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_single_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_single_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_mpi_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_mpi_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
```

## 18. Cosa riportare nella relazione

Single-node:

```text
impl, case, threads, avg_total_s, avg_sort_s, avg_merge_s, speedup, efficiency, generated_runs
```

Tuning:

```text
chunk_mb, merge_fan, generated_runs, avg_total_s, avg_sort_s, avg_merge_s
```

Strong scaling:

```text
nodes, ranks, threads_per_rank, total_cores, avg_total_s, strong_speedup, strong_efficiency
```

Weak scaling:

```text
nodes, records, records_per_node, threads_per_rank, avg_total_s, weak_efficiency
```

Nel commento finale discuti:

- il merge come collo di bottiglia;
- quanto migliora il sort aumentando i thread;
- se il tuning riduce il tempo di merge;
- differenza tra payload piccoli e payload grandi;
- overhead MPI e limite di comunicazione/I/O;
- eventuali problemi FastFlow osservati nei log.
