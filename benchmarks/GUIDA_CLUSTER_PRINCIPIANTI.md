# Guida per eseguire i benchmark sullo spmcluster

Questa guida parte da zero. L'idea e':

1. preparare il progetto da WSL;
2. copiarlo sul cluster usando PowerShell;
3. entrare nel cluster;
4. lanciare i benchmark con Slurm;
5. controllare lo stato dei job;
6. scaricare CSV, log e grafici.

Sostituisci sempre `LOGIN` con il tuo username del cluster, cioe' la parte prima di `@studenti.unipi.it`, tutta minuscola.

## 0. Prima di iniziare

Se sei fuori dalla rete UNIPI, attiva prima la VPN UNIPI.

Da PowerShell prova il login:

```powershell
ssh LOGIN@spmcluster.unipi.it
```

Se il login funziona, esci dal cluster:

```bash
exit
```

## 1. Creare un archivio del progetto da WSL

Esegui questi comandi da PowerShell, non dentro WSL.

Il progetto locale si trova in:

```text
/home/matti/spm projects/spm
```

Crea un archivio `.tar.gz` evitando build, dati e risultati vecchi:

```powershell
wsl bash -lc 'cd "/home/matti/spm projects" && tar --exclude=spm/build --exclude=spm/build_bench --exclude=spm/benchmark_data --exclude=spm/benchmark_results --exclude=spm/.git -czf /tmp/spm.tar.gz spm'
```

Copia l'archivio da WSL alla cartella temporanea Windows:

```powershell
wsl cp /tmp/spm.tar.gz "/mnt/c/Users/$env:USERNAME/AppData/Local/Temp/spm.tar.gz"
```

Controlla che il file esista:

```powershell
dir "$env:TEMP\spm.tar.gz"
```

## 2. Copiare il progetto sul cluster

Crea sul cluster due cartelle dedicate:

```powershell
ssh LOGIN@spmcluster.unipi.it "mkdir -p ~/spm_upload ~/spmProject"
```

Copia l'archivio:

```powershell
scp "$env:TEMP\spm.tar.gz" LOGIN@spmcluster.unipi.it:~/spm_upload/
```

Estrai il progetto dentro `~/spmProject`:

```powershell
ssh LOGIN@spmcluster.unipi.it "cd ~/spmProject && tar -xzf ~/spm_upload/spm.tar.gz --strip-components=1"
```

Nota importante: non copiare direttamente dentro `~/` con `rsync -av`. La home del cluster deve restare con permessi `700`. Usa sempre sottocartelle come `~/spm_upload` o `~/spmProject`.

## 3. Entrare nel cluster e controllare il progetto

Da PowerShell:

```powershell
ssh LOGIN@spmcluster.unipi.it
```

Ora sei sul cluster. Da qui in poi i comandi sono comandi Linux.

Vai nella cartella del progetto:

```bash
cd ~/spmProject
```

Controlla che ci siano gli script:

```bash
ls benchmarks
```

Controlla che gli script siano leggibili dalla shell:

```bash
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
```

## 4. Controllare gli strumenti disponibili

Sempre sul cluster, dentro `~/spmProject`:

```bash
which cmake
which g++
which python3
which sbatch
which squeue
which mpicxx || which mpic++ || which mpiCC
```

Se qualche comando manca, controlla i moduli disponibili:

```bash
module avail
```

Esempi tipici, se servono:

```bash
module load cmake
module load gcc
module load mpi
```

I nomi dei moduli possono cambiare sul cluster. Se `module load mpi` non funziona, usa `module avail` e scegli il modulo MPI disponibile.

## 5. Installare FastFlow, se manca

FastFlow serve per avere anche `ff_sort` nei benchmark single-node.

Controlla se esiste gia':

```bash
ls ~/fastFlow
```

Se non esiste:

```bash
cd ~
git clone https://github.com/fastflow/fastflow.git fastFlow
cd ~/spmProject
```

Quando lanci il benchmark single-node, passa:

```bash
FF_ROOT="$HOME/fastFlow"
```

Se FastFlow non e' installato, lo script esegue comunque OpenMP e salta FastFlow.

## 6. Primo test rapido single-node

Questo serve solo per controllare che tutto compili e giri.

```bash
cd ~/spmProject
BENCHMARK_CASES="quick:1000000:64" \
THREAD_LIST="1 2" \
TRIALS=1 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch benchmarks/slurm_single_node.sbatch
```

Il comando `sbatch` non esegue subito nel terminale: invia un job a Slurm.

Controlla la coda:

```bash
squeue -u $USER
```

Quando il job finisce, guarda gli output:

```bash
ls -lh slurm_single_*.out slurm_single_*.err
tail -n 80 slurm_single_*.out
tail -n 80 slurm_single_*.err
```

Se tutto va bene, trovi i risultati in:

```bash
ls benchmark_results
cat benchmark_results/single_node_summary.csv
```

## 7. Benchmark single-node serio

Questo e' il benchmark da usare per la relazione per OpenMP/FastFlow.

```bash
cd ~/spmProject
BENCHMARK_CASES="manySmall50M:50000000:64 payload512:5000000:512 payload2048:1000000:2048" \
THREAD_LIST="1 2 4 8 12 16 20 24 28 32" \
TRIALS=3 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch benchmarks/slurm_single_node.sbatch
```

Cosa produce:

```text
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
benchmark_results/plots/
benchmark_results/*.log
slurm_single_JOBID.out
slurm_single_JOBID.err
```

Il job single-node usa `node01`. Gli input e i temporanei vengono creati sotto `/scratch/$USER/spmRun/JOBID` se disponibile, altrimenti sotto `/tmp/$USER/spmRun/JOBID`. I CSV restano in `benchmark_results`.

## 8. Benchmark diagnostico del merge

Questo non e' il benchmark principale. Serve solo per capire se il merge crea gruppi paralleli.

```bash
cd ~/spmProject
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 2 4 8 16" \
CHUNK_MB=64 \
MERGE_FAN=16 \
MERGE_VERBOSE=1 \
TRIALS=2 \
VERIFY=1 \
FF_ROOT="$HOME/fastFlow" \
sbatch benchmarks/slurm_single_node.sbatch
```

Poi cerca le righe `[merge]` nei log:

```bash
grep -R "\[merge\]" benchmark_results/*.log | head -n 40
```

Interpretazione rapida:

```text
groups=1
```

vuol dire che in quella passata il merge non aveva gruppi indipendenti da parallelizzare.

```text
groups=4 mode=parallel
```

vuol dire che in quella passata il merge ha creato 4 gruppi indipendenti.

## 9. Benchmark MPI strong e weak scaling

Questo usa `node01-node08`, cioe' gli 8 nodi omogenei. Evita `node09`.

Run seria consigliata:

```bash
cd ~/spmProject
BENCHMARK_CASES="manySmall50M:50000000:64" \
WEAK_CASES="weakSmall:10000000:64" \
MPI_THREAD_LIST="1 2 4 8 16" \
TRIALS=3 \
VERIFY=1 \
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

Cosa misura:

- strong scaling: stesso input, nodi 1, 2, 4, 8;
- weak scaling: input che cresce con i nodi;
- thread per processo MPI: `1 2 4 8 16`.

Cosa produce:

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

Nota: nel job MPI il dataset resta in `benchmark_data/`, dentro il progetto, perche' deve essere visibile da tutti i nodi. I temporanei dei rank vanno invece sotto `/scratch` o `/tmp`.

## 10. Weak scaling con payload piu' grande

Se hai tempo, puoi aggiungere un job solo weak con payload piu' grande:

```bash
cd ~/spmProject
RUN_STRONG=0 \
RUN_WEAK=1 \
WEAK_CASES="weakPayload512:2000000:512 weakPayload2048:250000:2048" \
MPI_THREAD_LIST="1 2 4 8 16" \
TRIALS=3 \
VERIFY=1 \
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

Questo serve per discutere cosa succede quando aumenta il peso dell'I/O rispetto all'ordinamento.

## 11. Comandi Slurm essenziali

Vedere i tuoi job:

```bash
squeue -u $USER
```

Vedere tutti i job in coda:

```bash
squeue
```

Cancellare un job:

```bash
scancel JOBID
```

Vedere i file prodotti:

```bash
ls -lh
ls -lh benchmark_results
```

Leggere la fine di un output:

```bash
tail -n 80 slurm_single_*.out
tail -n 80 slurm_mpi_*.out
```

Leggere errori:

```bash
tail -n 120 slurm_single_*.err
tail -n 120 slurm_mpi_*.err
```

Seguire un file mentre il job sta girando:

```bash
tail -f slurm_single_*.out
```

Per uscire da `tail -f`, premi `Ctrl+C`.

## 12. Rigenerare summary e grafici

Di solito gli script Slurm lo fanno gia'. Se vuoi rifarlo a mano:

```bash
cd ~/spmProject
python3 benchmarks/analyze.py --results-dir benchmark_results
```

Controlla i CSV:

```bash
head benchmark_results/single_node_summary.csv
head benchmark_results/mpi_strong_summary.csv
head benchmark_results/mpi_weak_summary.csv
```

Controlla i grafici:

```bash
ls benchmark_results/plots
```

Se `matplotlib` non e' installato, i CSV vengono comunque prodotti. I grafici PNG sono opzionali.

## 13. Scaricare i risultati su Windows

Esci dal cluster:

```bash
exit
```

Ora sei di nuovo in PowerShell.

Crea una cartella sul Desktop:

```powershell
mkdir "$env:USERPROFILE\Desktop\spm_benchmark_results"
```

Scarica i risultati:

```powershell
scp -r LOGIN@spmcluster.unipi.it:~/spmProject/benchmark_results "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spmProject/slurm_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
```

## 14. File importanti per la relazione

Usa soprattutto:

```text
single_node_summary.csv
mpi_strong_summary.csv
mpi_weak_summary.csv
benchmark_results/plots/
slurm_*.out
benchmark_results/*.log
```

Nel single-node guarda:

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

Nel strong scaling guarda:

```text
nodes
ranks
threads_per_rank
total_cores
avg_total_s
strong_speedup
strong_efficiency
```

Nel weak scaling guarda:

```text
nodes
ranks
threads_per_rank
records
avg_total_s
weak_efficiency
```

## 15. Se qualcosa va male

Se il job fallisce subito:

```bash
tail -n 120 slurm_single_*.err
tail -n 120 slurm_mpi_*.err
```

Se manca FastFlow:

```bash
ls ~/fastFlow
FF_ROOT="$HOME/fastFlow" sbatch benchmarks/slurm_single_node.sbatch
```

Se manca MPI:

```bash
which mpicxx || which mpic++ || which mpiCC
module avail
```

Se non riesci piu' a fare SSH e sospetti permessi della home, serve una sessione ancora aperta e questo comando:

```bash
chmod 700 ~
```

Per evitare il problema, non copiare mai archivi direttamente in `~/` con opzioni archive tipo `rsync -av`.

