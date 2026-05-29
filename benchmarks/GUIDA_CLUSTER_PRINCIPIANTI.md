# Guida principianti per i benchmark sullo spmcluster

Questa guida assume che il progetto venga portato sul cluster tramite GitHub. I comandi Windows vanno eseguiti da PowerShell. I comandi Linux vanno eseguiti dopo il login SSH sul cluster.

Sostituisci sempre:

```text
LOGIN
```

con il tuo username del cluster, cioe' la parte prima di `@studenti.unipi.it`, tutta minuscola.

Sostituisci:

```text
GITHUB_URL
```

con l'URL del tuo repository GitHub.

## 1. Entrare nel cluster

Se sei fuori dalla rete UNIPI, attiva prima la VPN UNIPI.

Da PowerShell:

```powershell
ssh LOGIN@spmcluster.unipi.it
```

Se il login funziona, sei sul frontend del cluster.

## 2. Scaricare il progetto da GitHub

Sul cluster:

```bash
cd ~
git clone GITHUB_URL spmProject
cd ~/spmProject
```

Se il progetto era gia' stato clonato:

```bash
cd ~/spmProject
git pull
```

Se il repository e' privato, usa l'accesso GitHub che hai configurato: HTTPS con token oppure SSH key GitHub. Il cluster deve poter leggere il repository.

## 3. Controllare strumenti e script

Dentro `~/spmProject`:

```bash
which cmake
which g++
which python3
which sbatch
which squeue
which mpicxx || which mpic++ || which mpiCC
```

Se qualche comando manca, guarda i moduli disponibili:

```bash
module avail
```

E carica quelli necessari, per esempio:

```bash
module load cmake
module load gcc
module load mpi
```

I nomi precisi dei moduli possono cambiare sul cluster.

Controlla che gli script siano sintatticamente corretti:

```bash
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
```

## 4. Installare FastFlow

FastFlow serve per produrre anche i risultati `ff`.

Sul cluster:

```bash
cd ~
git clone https://github.com/fastflow/fastflow.git fastFlow
cd ~/spmProject
```

Se `~/fastFlow` esiste gia':

```bash
ls ~/fastFlow
```

Quando lanci i benchmark single-node, passa sempre:

```bash
FF_ROOT="$HOME/fastFlow"
```

Se FastFlow manca, gli script eseguono OpenMP e saltano FastFlow.

## 5. Parametri scelti

Rispetto a `benchmarks_others`, questa suite misura le stesse cose principali ma in modo piu' leggibile:

- single-node OpenMP/FastFlow;
- payload diversi;
- MPI strong scaling;
- MPI weak scaling;
- CSV aggregati con speedup ed efficiency;
- log con `Fase 1`, `Fase 2`, `Totale`.

Parametri consigliati:

```bash
CHUNK_MB=128
MERGE_FAN=8
```

`CHUNK_MB=128` tiene basso il numero di file temporanei senza eliminare il parallelismo.

`MERGE_FAN=8` e' meglio del vecchio `64`, perche' con poche decine di run temporanee evita che il merge finisca in un unico gruppo quasi seriale.

Per misure finali usa:

```bash
VERIFY=0
```

La verifica legge di nuovo input e output, quindi aggiunge I/O che sporca il tempo misurato. Usa `VERIFY=1` nei test rapidi e in una sola run di controllo correttezza.

Gli script stampano quante run stanno per eseguire. Esempio:

```text
[bench] Run single-node pianificate: 60
```

Se vedi un numero enorme, probabilmente hai messo troppi casi nella stessa job.

## 6. Test rapido single-node

Serve solo per controllare che compili e parta.

```bash
cd ~/spmProject
BENCHMARK_CASES="quick:1000000:64" \
THREAD_LIST="1 2" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

Controlla lo stato:

```bash
squeue -u $USER
```

Quando finisce:

```bash
tail -n 80 slurm_single_*.out
tail -n 80 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
```

## 7. Benchmark single-node principale

Questo e' il benchmark principale per speedup ed efficiency OpenMP/FastFlow. Usa molti record e payload piccolo.

```bash
cd ~/spmProject
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 12 16 20 24 28 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=3 \
VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

Questa job produce:

```text
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
benchmark_results/plots/
benchmark_results/*.log
slurm_single_JOBID.out
slurm_single_JOBID.err
```

## 8. Benchmark payload diversi

Questo serve per rispettare bene la parte della consegna sulla distribuzione del payload. Va lanciato come seconda job.

```bash
cd ~/spmProject
BENCHMARK_CASES="payload16:20000000:16 payload512:5000000:512 payload2048:31250:2048" \
THREAD_LIST="1 4 8 16 32" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=2 \
VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
APPEND_RESULTS=1 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

`APPEND_RESULTS=1` e' importante: aggiunge queste righe al CSV single-node gia' prodotto dalla job principale.

## 9. Benchmark MPI strong e weak scaling

Questo usa `node01-node08`, evitando `node09` perche' e' diverso.

```bash
cd ~/spmProject
BENCHMARK_CASES="manySmall50M:50000000:64" \
WEAK_CASES="weakSmall10M:10000000:64" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 2 4 8 16" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=3 \
VERIFY=0 \
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

Cosa misura:

- strong scaling: stesso dataset, nodi 1, 2, 4, 8;
- weak scaling: record per nodo costanti, dataset totale crescente;
- processi MPI: 1, 2, 4, 8, perche' usiamo un rank per nodo;
- thread per processo: `1 2 4 8 16`.

Output principali:

```text
benchmark_results/mpi_strong_raw.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_raw.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/plots/
benchmark_results/*.log
slurm_mpi_JOBID.out
slurm_mpi_JOBID.err
```

## 10. Weak scaling con payload piu' grande

Questo e' opzionale. Fallo solo se hai tempo.

```bash
cd ~/spmProject
RUN_STRONG=0 \
RUN_WEAK=1 \
WEAK_CASES="weakPayload512:2000000:512 weakPayload2048:250000:2048" \
STRONG_NODES="1 2 4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 2 4 8 16" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=3 \
VERIFY=0 \
APPEND_RESULTS=1 \
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

Serve per discutere cosa succede quando il payload cresce e aumenta il peso dell'I/O.

## 11. Diagnostica merge

Non e' una misura principale. Serve per capire se il merge sta creando gruppi paralleli.

```bash
cd ~/spmProject
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 2 4 8 16" \
CHUNK_MB=64 \
MERGE_FAN=8 \
MERGE_VERBOSE=1 \
TRIALS=2 \
VERIFY=0 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

Poi:

```bash
grep -R "\[merge\]" benchmark_results/*.log | head -n 40
```

Interpretazione:

```text
groups=1
```

vuol dire che quella passata del merge non aveva gruppi indipendenti.

```text
groups=4 mode=parallel
```

vuol dire che quella passata ha creato gruppi paralleli.

## 12. Run di controllo correttezza

Dopo i benchmark finali, fai una piccola run con verifica attiva. Non serve per le curve, serve solo per dire nella relazione che hai controllato la correttezza dell'output.

```bash
cd ~/spmProject
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
CHUNK_MB=128 \
MERGE_FAN=8 \
TRIALS=1 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

## 13. Comandi Slurm utili

Vedere i tuoi job:

```bash
squeue -u $USER
```

Cancellare una job:

```bash
scancel JOBID
```

Vedere la fine degli output:

```bash
tail -n 80 slurm_single_*.out
tail -n 80 slurm_mpi_*.out
```

Vedere gli errori:

```bash
tail -n 120 slurm_single_*.err
tail -n 120 slurm_mpi_*.err
```

Seguire una job mentre gira:

```bash
tail -f slurm_single_*.out
```

Per uscire da `tail -f`, premi `Ctrl+C`.

## 14. Rigenerare summary e grafici

Di solito gli script Slurm chiamano gia' `analyze.py`. Se vuoi rifarlo a mano:

```bash
cd ~/spmProject
python3 benchmarks/analyze.py --results-dir benchmark_results
```

I file importanti sono:

```text
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/plots/
```

Se `matplotlib` non e' installato, i CSV vengono prodotti comunque. I grafici PNG sono opzionali.

## 15. Preparare un archivio risultati sul cluster

Quando hai finito le misure:

```bash
cd ~/spmProject
tar -czf spm_benchmark_results.tar.gz benchmark_results slurm_single_*.out slurm_single_*.err slurm_mpi_*.out slurm_mpi_*.err
```

Controlla:

```bash
ls -lh spm_benchmark_results.tar.gz
```

## 16. Spostare i risultati su Windows

Esci dal cluster:

```bash
exit
```

Da PowerShell:

```powershell
mkdir "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/spm_benchmark_results.tar.gz "$env:USERPROFILE\Desktop\spm_benchmark_results\"
```

Se vuoi scaricare direttamente la cartella invece dell'archivio:

```powershell
scp -r LOGIN@spmcluster.unipi.it:~/spmProject/benchmark_results "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_single_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_single_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_mpi_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_mpi_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
```

## 17. Cosa usare nella relazione

Per single-node:

```text
impl
case
threads
avg_total_s
avg_sort_s
avg_merge_s
speedup
efficiency
generated_runs
```

Per strong scaling:

```text
nodes
ranks
threads_per_rank
total_cores
avg_total_s
strong_speedup
strong_efficiency
```

Per weak scaling:

```text
nodes
ranks
threads_per_rank
records
records_per_node
avg_total_s
weak_efficiency
```

Nei log puoi commentare:

```text
Fase 1
Fase 2
Totale
```

Queste tre misure bastano per discutere bottleneck, merge, I/O e overhead senza introdurre microbenchmark inutili.
