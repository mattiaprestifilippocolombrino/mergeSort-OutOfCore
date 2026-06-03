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

Per i benchmark finali con `PAYLOAD_MAX_BUILD=4096` non aggiungere
`SKIP_BUILD=1` ai comandi: gli script devono ricompilare il progetto con quel
payload massimo. Se usi `SKIP_BUILD=1` per prove veloci, gli script controllano
la build esistente e si fermano se e' stata compilata con un `PAYLOAD_MAX`
troppo piccolo.

## 4. Cosa significano Fase 1 e Fase 2

Gli script usano il merge **multi-pass semplice** come modalita' standard:

- OpenMP e FastFlow passano `--multipass-merge` in modo esplicito.
- MPI usa il merge locale multi-pass di default.
- Il parametro `MERGE_FAN` regola il fan-in del K-way merge multi-pass.

Per confrontare con le altre modalita' puoi impostare:

```bash
OMP_FLAT_MERGE=1
FF_FLAT_MERGE=1
OMP_PIPELINE=1
FF_PIPELINE=1
MPI_PIPELINE_LOCAL_MERGE=1
MPI_FLAT_LOCAL_MERGE=1
```

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
total_speedup, total_efficiency
phase1_speedup, phase1_efficiency
phase2_speedup, phase2_efficiency
```

MPI strong:

```text
nodes, ranks, threads_per_rank, total_cores
avg_total_s, avg_sort_s, avg_merge_s
total_speedup, total_efficiency
phase1_speedup, phase1_efficiency
phase2_speedup, phase2_efficiency
```

MPI weak:

```text
nodes, records, records_per_node
avg_total_s, avg_sort_s, avg_merge_s
total_speedup, total_efficiency
phase1_speedup, phase1_efficiency
phase2_speedup, phase2_efficiency
```

Formule:

```text
T_seq = tempo della versione a 1 thread/worker
T_n = tempo della versione con n thread/worker

total_speedup = T_seq_total / T_n_total
phase1_speedup = T_seq_fase1 / T_n_fase1
phase2_speedup = T_seq_fase2 / T_n_fase2

single-node efficiency = speedup / threads
strong efficiency = speedup / (nodes / baseline_nodes)
weak efficiency = speedup
```

Per i benchmark single-node questa e' esattamente la definizione classica di
speedup: tempo sequenziale diviso tempo con n thread. Per MPI strong, invece,
lo summary misura lo scaling rispetto alla riga con meno nodi nella stessa
configurazione `threads_per_rank`; per uno speedup rispetto alla sequenziale
pura usa come riferimento `nodes=1`, `ranks=1`, `threads_per_rank=1`.

Nel summary single-node restano anche gli alias storici:

```text
speedup, efficiency
sort_speedup, sort_efficiency
merge_speedup, merge_efficiency
```

Sono equivalenti rispettivamente a totale, fase 1 e fase 2.

## 6. Smoke test

Serve solo a verificare build, esecuzione e verifier.

> [!TIP]
> **I valori di CHUNK_MB=128 e MERGE_FAN=16 nei comandi seguenti sono puramente indicativi (anche se migliori di quelli vecchi)!**
> Le prestazioni ottimali dipendono dall'hardware del cluster. Prima di avviare campagne di misurazione lunghe, vedi la sezione **14. Eseguire il Tuning** per lanciare il job di tuning e scoprire i tuoi valori ideali.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="quick:1000000:64" \
THREAD_LIST="1 2" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
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
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
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

Nel report commenta soprattutto la differenza tra `phase1_efficiency` e
`phase2_efficiency`: il sort tende a scalare meglio, il merge e l'I/O limitano
il totale.

## 8. FastFlow single-node

Lancialo separato da OpenMP.

```bash
cd ~/spmProject
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
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

## 8 bis. Confrontare multi-pass, pipeline e flat

Per capire se la pipeline o il flat migliorano rispetto allo standard, fai run
uguali cambiando solo la variabile di merge.

OpenMP multi-pass standard:

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

OpenMP pipeline:

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 OMP_PIPELINE=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

OpenMP flat:

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 OMP_FLAT_MERGE=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

Per FastFlow usa `RUN_OMP=0 RUN_FF=1`, poi confronta la run standard contro
`FF_PIPELINE=1` e `FF_FLAT_MERGE=1`.

Nel confronto guarda:

```text
avg_total_s
total_speedup, total_efficiency
phase1_speedup, phase1_efficiency
phase2_speedup, phase2_efficiency
```

Il multi-pass semplice e' il riferimento principale. La pipeline puo' aiutare
quando la Fase 2 pesa molto, mentre su input piccoli il flat puo' ancora vincere
per overhead minore.

## 9. Payload distribution

Serve a confrontare molti record piccoli con meno record e payload piu' grande.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="mediumPayload8M:8000000:512 largePayload2M:2000000:2048" \
THREAD_LIST="1 8 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
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
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
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
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
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
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=16 \
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

## 14. Eseguire il Tuning (Opzionale ma raccomandato)

Puoi lanciare il job di tuning per esplorare varie configurazioni di chunk e fan-in in modo automatico. È essenziale farlo prima di lanciare i job per i benchmark finali, così da massimizzare le performance per l'I/O del cluster.

```bash
cd ~/spmProject
sbatch benchmarks/slurm_tune_single_node.sbatch
```

Il job proverà in automatico tutte le combinazioni (es. chunk 64, 128, 256 MB con fan-in 16, 32, 64) e genererà un CSV chiamato `single_node_tuning_raw.csv` con la classifica delle combinazioni ottimali, che poi verrà plottata dallo script di analisi.

## 15. Scaricare i risultati su Windows

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

## 16. Cosa scrivere nella relazione

Commenta:

- speedup ed efficiency totali;
- speedup/efficiency della Fase 1 e della Fase 2;
- perché il merge scala meno del sort;
- payload piccoli contro payload grandi;
- Amdahl per strong scaling;
- Gustafson per weak scaling;
- eventuali run FastFlow non valide, con riferimento ai log.
