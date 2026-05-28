# Guida rapida alle misurazioni sullo spmcluster

Questa guida serve per eseguire i benchmark del progetto quando il codice sta in WSL e l'accesso SSH al cluster viene fatto da PowerShell.

## Cosa cambierei nelle misurazioni

La struttura attuale va bene: gli script misurano single-node, strong scaling MPI, weak scaling MPI e producono CSV con `Fase 1`, `Fase 2` e `Totale`.

Non aggiungerei microbenchmark separati per "lettura chunk", "sort chunk" o "merge k-way": la consegna chiede curve di prestazione e un modello di costo approssimato, non la misura isolata di ogni sottofase. Per l'analisi bastano:

- `Fase 1`: generazione delle run ordinate;
- `Fase 2`: merge locale e, nella versione MPI, merge distribuito;
- `Totale`: tempo end-to-end da usare per speedup, efficienza, strong e weak scaling.

Le modifiche pratiche applicate agli script sono queste:

1. Eseguire il single-node su `node01` e l'MPI su `node01-node08`, evitando `node09` perche' ha architettura diversa.
2. Usare almeno due profili di dataset:
   - molti record con payload piccolo, per stressare ordinamento e scheduling;
   - pochi record con payload grande, per stressare I/O e movimento dati.
3. Per il weak scaling si puo' usare `WEAK_CASES` per fare una run con payload piccolo e, se c'e' tempo, una run aggiuntiva con payload grande.
4. Tenere `TRIALS=5` e usare `analyze.py`, che scarta il trial peggiore se ci sono almeno 3 ripetizioni.
5. Conservare i log `.out`, `.err` e `benchmark_results/*.log`, per discutere bottleneck e fasi lente nella relazione.

## 1. Login al cluster da PowerShell

Sostituisci `LOGIN` con il tuo username, cioe' la parte prima di `@studenti.unipi.it`, tutto minuscolo.

```powershell
ssh LOGIN@spmcluster.unipi.it
```

Se sei fuori dalla rete UNIPI, prima attiva la VPN UNIPI.

Sul cluster crea una directory dedicata:

```powershell
ssh LOGIN@spmcluster.unipi.it "mkdir -p ~/spm_upload ~/spm"
```

## 2. Preparare il progetto da WSL

Da PowerShell puoi creare un archivio prendendo i file direttamente da WSL. Questo evita di copiare build locali, risultati vecchi e file grandi.

```powershell
wsl bash -lc 'cd "/home/matti/spm projects" && tar --exclude=spm/build --exclude=spm/build_bench --exclude=spm/benchmark_data --exclude=spm/benchmark_results --exclude=spm/.git -czf /tmp/spm.tar.gz spm'
```

Copia l'archivio dalla filesystem WSL alla cartella temporanea Windows:

```powershell
$WINUSER = $env:USERNAME
wsl cp /tmp/spm.tar.gz "/mnt/c/Users/$WINUSER/AppData/Local/Temp/spm.tar.gz"
```

## 3. Copiare il progetto sul cluster

Da PowerShell:

```powershell
scp "$env:TEMP\spm.tar.gz" LOGIN@spmcluster.unipi.it:~/spm_upload/
```

Poi estrai sul cluster:

```powershell
ssh LOGIN@spmcluster.unipi.it "cd ~/spm && tar -xzf ~/spm_upload/spm.tar.gz --strip-components=1"
```

Attenzione: evita comandi tipo questo:

```powershell
rsync -av data LOGIN@spmcluster.unipi.it:~/
```

Meglio copiare sempre dentro una sottodirectory, per esempio `~/spm` o `~/spm_upload`. Copiare direttamente su `~/` con opzioni archive puo' cambiare i permessi della home. La home sul cluster deve restare a permessi `700`.

Se succede e hai ancora una sessione aperta:

```bash
chmod 700 ~
```

## 4. Entrare nel progetto sul cluster

```powershell
ssh LOGIN@spmcluster.unipi.it
```

Poi, sul cluster:

```bash
cd ~/spm
ls benchmarks
```

Se vuoi controllare che gli script siano eseguibili:

```bash
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
```

## 5. Benchmark single-node

Questo misura OpenMP e FastFlow variando i thread su un nodo. Lo script Slurm e' vincolato a `node01`, che fa parte dei nodi omogenei.

Uso consigliato:

```bash
sbatch benchmarks/slurm_single_node.sbatch
```

Lo script usa di default:

```bash
THREAD_LIST="1 2 4 8 16 32"
TRIALS=5
VERIFY=1
```

Output principali:

```text
slurm_single_JOBID.out
slurm_single_JOBID.err
benchmark_results/single_node_raw.csv
benchmark_results/single_node_summary.csv
benchmark_results/plots/
```

## 6. Benchmark MPI strong e weak scaling

Questo misura MPI fino a 8 nodi. Lo script Slurm usa solo i nodi omogenei `node01-node08`:

```bash
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

Lo script usa di default:

```bash
STRONG_NODES="1 2 4 8"
RANKS_PER_NODE=1
MPI_THREAD_LIST="1 2 4 8 16"
TRIALS=5
VERIFY=1
```

Con questi valori il numero di processi MPI cresce con i nodi: 1, 2, 4, 8. I thread per processo cambiano con `MPI_THREAD_LIST`. Questo copre la richiesta senza moltiplicare troppo il numero di run.

Output principali:

```text
slurm_mpi_JOBID.out
slurm_mpi_JOBID.err
benchmark_results/mpi_strong_raw.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_raw.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/plots/
```

## 7. Weak scaling con piu' profili

La run MPI standard basta gia' per strong scaling e per un weak scaling con un profilo. Se hai tempo, puoi usare `WEAK_CASES` per misurare sia payload piccolo sia payload grande nella stessa job:

```bash
WEAK_CASES="weak_small:1000000:64 weak_large:2048:1048576" \
TRIALS=5 \
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

`weak_small` stressa il caso con molti record e payload piccolo. `weak_large` stressa il caso opposto: meno record, payload molto grande, quindi maggiore pressione su I/O e trasferimenti.

Se invece vuoi aggiungere solo il weak scaling con payload grande senza ripetere lo strong scaling:

```bash
RUN_STRONG=0 \
RUN_WEAK=1 \
WEAK_CASES="weak_large:2048:1048576" \
TRIALS=5 \
sbatch benchmarks/slurm_mpi_scaling.sbatch
```

## 8. Controllare lo stato dei job

```bash
squeue -u $USER
```

Per leggere gli output:

```bash
tail -n 80 slurm_single_*.out
tail -n 80 slurm_mpi_*.out
```

Se un job fallisce, guarda prima:

```bash
tail -n 120 slurm_single_*.err
tail -n 120 slurm_mpi_*.err
```

## 9. Rigenerare i summary a mano

Di solito gli script Slurm chiamano gia' `analyze.py`. Se vuoi rigenerare i CSV aggregati:

```bash
python3 benchmarks/analyze.py --results-dir benchmark_results
```

I file da usare nella relazione sono soprattutto:

```text
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
```

## 10. Scaricare i risultati dal cluster a Windows

Da PowerShell:

```powershell
scp -r LOGIN@spmcluster.unipi.it:~/spm/benchmark_results "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spm/slurm_*.out "$env:USERPROFILE\Desktop\spm_benchmark_results"
scp LOGIN@spmcluster.unipi.it:~/spm/slurm_*.err "$env:USERPROFILE\Desktop\spm_benchmark_results"
```

## 11. Cosa riportare nella relazione

Per la parte performance:

- grafici single-node: speedup ed efficienza per OpenMP e FastFlow;
- grafici strong scaling MPI: tempo, speedup ed efficienza fino a 8 nodi;
- grafici weak scaling MPI: efficienza debole fino a 8 nodi;
- confronto tra `many_small` e `few_large`;
- commento su `Fase 1` e `Fase 2`.

Per il modello di costo basta un modello approssimato:

```text
T_total ~= T_boundary + T_run_generation + T_local_merge + T_mpi_tree_merge
```

Dove:

```text
T_run_generation ~= I/O input + sort parallelo dei chunk + scrittura run
T_local_merge    ~= merge k-way locale + I/O temporaneo
T_mpi_tree_merge ~= comunicazione MPI + merge ad albero
```

Questo e' coerente con quello che misurano gli script, perche' i programmi stampano gia' `Fase 1`, `Fase 2` e `Totale`.
