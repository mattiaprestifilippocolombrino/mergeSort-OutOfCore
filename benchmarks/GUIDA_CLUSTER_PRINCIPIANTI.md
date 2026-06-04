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

Prima di sottomettere job Slurm crea la tua area scratch:

```bash
cd ~/spmProject
./benchmarks/setup_scratch.sh
```

Dal login node questo comando sottomette un piccolo job Slurm: `/scratch` va
creata/verificata sui nodi di calcolo, non sul login node.

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
Gli script puliscono `RUN_DIR` su `/scratch` a fine job (`CLEAN_SCRATCH=1`).
Metti `CLEAN_SCRATCH=0` solo se devi ispezionare file temporanei dopo un errore.

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

Le vecchie modalita' pipeline e flat sono state spostate nelle cartelle
`legacy` e non fanno parte dei benchmark correnti.

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

MPI weak capacity:

```text
nodes, records, records_per_node
avg_total_s, avg_sort_s, avg_merge_s
avg_capacity_gib_per_node, avg_capacity_total_gib
avg_throughput_gib_node_s, avg_throughput_gib_s
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
> **Versione finale:** nei comandi seguenti usa `CHUNK_MB=64` e `MERGE_FAN=8`.

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
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
RUN_TIMEOUT_SECONDS=900 \
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

## 8 bis. Varianti legacy

Pipeline e flat sono archiviate nelle cartelle `legacy`; i benchmark correnti
misurano solo il multi-pass semplice.

## 10. MPI strong scaling

Dataset fisso, nodi crescenti. Lo script copia l'input su `/scratch` locale dei
nodi prima della misura, quindi la copia non entra in Fase 1/Fase 2/Totale.

Job finali strong, uno per punto della curva:

```bash
cd ~/spmProject
for n in 1 2 4 8; do
  for t in 1 4 8 16 32; do
    RUN_STRONG=1 RUN_WEAK=0 \
    BENCHMARK_CASES="manySmall50M:50000000:64" \
    STRONG_NODES="$n" \
    RANKS_PER_NODE=1 \
    MPI_THREAD_LIST="$t" \
    PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
    TRIALS=1 VERIFY=0 \
    sbatch --nodes="$n" --time=00:29:00 benchmarks/slurm_mpi_scaling.sbatch
  done
done
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

## 11. MPI weak capacity

La weak finale non passa piu' una dimensione statica del dataset. Per ogni
coppia `(nodi, thread/rank)` lo script genera una sonda interna derivata da
`CHUNK_MB`, `MERGE_FAN` e `WEAK_PROBE_CHUNKS_PER_RANK`, misura il throughput e
lo normalizza su `WEAK_TIME_BUDGET_SECONDS=180`. Il risultato da usare nel
report e' quanti GiB vengono processati in 3 minuti per nodo e in totale.

Job finali weak, uno per coppia `(nodi, thread/rank)`:

```bash
cd ~/spmProject
for n in 1 2 4 8; do
  for t in 1 4 8 16 32; do
    RUN_STRONG=0 RUN_WEAK=1 \
    STRONG_NODES="$n" \
    RANKS_PER_NODE=1 \
    MPI_THREAD_LIST="$t" \
    PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
    WEAK_TIME_BUDGET_SECONDS=180 \
    TRIALS=1 VERIFY=0 \
    sbatch --nodes="$n" --time=00:03:00 benchmarks/slurm_mpi_scaling.sbatch
  done
done
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

## 14. Eseguire il Tuning (Opzionale ma raccomandato)

Segui la guida dedicata:

```text
benchmarks/GUIDA_TUNING_OPENMP.md
```

Il tuning usa solo OpenMP e divide la grid search in piu' job brevi.

```bash
cd ~/spmProject
./benchmarks/setup_scratch.sh
```

## 15. Scaricare i risultati su Windows

Sul cluster:

```bash
cd ~/spmProject
tar -czf spm_benchmark_results.tar.gz benchmark_results
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
