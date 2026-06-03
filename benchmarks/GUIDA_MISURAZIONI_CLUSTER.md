# Guida rapida misurazioni cluster

Questa guida contiene la sequenza consigliata per produrre i risultati finali.
Ogni job crea una cartella dedicata in `/scratch/m.prestifilippoco/spmRun/results/run_<jobid>/`, con
CSV, summary, grafici e log separati.
I file di lavoro e i dataset temporanei stanno sotto `/scratch` e vengono
rimossi a fine job; imposta `CLEAN_SCRATCH=0` solo per debug.

## Parametri consigliati

> [!TIP]
> **I valori di CHUNK_MB e MERGE_FAN sono solo esempi!**
> Le prestazioni ottimali dipendono fortemente dall'hardware del cluster (disco NVMe vs HDD, latenza di rete, RAM disponibile).
> **Prima** di eseguire le misurazioni descritte qui sotto, lancia lo script di tuning per trovare i tuoi valori ideali:
> ```bash
> sbatch benchmarks/slurm_tune_single_node.sbatch
> ```
> Sostituisci poi `128` negli script sottostanti con il valore ottimale ricavato. Per ora `MERGE_FAN` resta fissato a `64`.

```bash
CHUNK_MB=128    # <--- SOSTITUISCI CON IL TUO VALORE OTTIMALE
MERGE_FAN=64
PAYLOAD_MAX_BUILD=4096
TRIALS=1
VERIFY=0
```

Nei CSV verrà indicata la `local_merge_impl` o `merge_impl` come **multipass**:
questo rappresenta il merge multi-pass semplice, scelto come versione principale
perche' e' prevedibile su cluster HPC e non crea writer thread extra. OpenMP e
FastFlow passano `--multipass-merge` in modo esplicito; MPI usa il merge locale
multi-pass di default. Il parametro `MERGE_FAN` controlla il fan-in massimo per
ogni merge.

Le versioni pipeline e flat sono materiale legacy: i file sono nelle cartelle
`legacy` e non sono usati dagli script di misura correnti.

Per le misure finali con `PAYLOAD_MAX_BUILD=4096` non impostare
`SKIP_BUILD=1`: gli script devono ricompilare con lo stesso valore. `SKIP_BUILD=1`
va bene solo per run rapide quando sei sicuro che `BUILD_DIR` sia gia' stato
configurato con un `PAYLOAD_MAX` almeno pari a `PAYLOAD_MAX_BUILD`; in caso
contrario gli script ora bloccano la run leggendo `BUILD_DIR/CMakeCache.txt`.

Tutti i log si trovano nella cartella `results/logs`. Qui è possibile leggere la scomposizione per "Fase 1 (sort)" e "Fase 2 (merge)".

## Preparazione

```bash
cd ~/spmProject
git pull
chmod +x benchmarks/*.sh benchmarks/*.sbatch
bash -n benchmarks/*.sh benchmarks/*.sbatch
python3 -m py_compile benchmarks/analyze.py
./benchmarks/setup_scratch.sh
```

## Layout risultati

Ogni job scrive in una cartella propria:

```text
/scratch/m.prestifilippoco/spmRun/results/
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
ls -td /scratch/m.prestifilippoco/spmRun/results/run_* | head -n 1
```

Esempio:

```bash
RUN_DIR="$(ls -td /scratch/m.prestifilippoco/spmRun/results/run_* | head -n 1)"
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
T_seq = tempo della versione a 1 thread/worker
T_n = tempo della versione con n thread/worker
total_speedup = T_seq_total / T_n_total
total_efficiency = total_speedup / n
phase1_speedup = T_seq_fase1 / T_n_fase1
phase1_efficiency = phase1_speedup / n
phase2_speedup = T_seq_fase2 / T_n_fase2
phase2_efficiency = phase2_speedup / n
```

MPI strong:

```text
total_speedup = T_base / T_p
total_efficiency = total_speedup / (nodes / baseline_nodes)
phase1_speedup, phase1_efficiency
phase2_speedup, phase2_efficiency
```

Per MPI strong `T_base` e' la run con il numero minimo di nodi disponibile
nella stessa configurazione `threads_per_rank`. Se vuoi uno speedup rispetto
alla sequenziale pura, confronta con la riga `nodes=1`, `ranks=1`,
`threads_per_rank=1`.

MPI weak:

```text
total_speedup = T_base / T_p
total_efficiency = total_speedup
phase1_speedup = sort_base / sort_p
phase1_efficiency = phase1_speedup
phase2_speedup = merge_base / merge_p
phase2_efficiency = phase2_speedup
```

Gli alias storici `speedup`, `efficiency`, `sort_speedup`, `sort_efficiency`,
`merge_speedup`, `merge_efficiency` restano nei summary single-node per
compatibilita' con i grafici.

I grafici in `plots/` mostrano insieme total, phase 1 e phase 2.

## OpenMP single-node

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=64 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

Controllo:

```bash
squeue -u "$USER"
tail -f /scratch/m.prestifilippoco/spmRun/slurm/slurm_single_*.out
tail -n 120 /scratch/m.prestifilippoco/spmRun/slurm/slurm_single_*.err
RUN_DIR="$(ls -td /scratch/m.prestifilippoco/spmRun/results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## FastFlow single-node

Eseguilo separato da OpenMP, cosi' eventuali problemi FastFlow non sporcano la
campagna OpenMP.

```bash
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=64 \
TRIALS=1 VERIFY=0 \
RUN_TIMEOUT_SECONDS=900 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

Controllo:

```bash
RUN_DIR="$(ls -td /scratch/m.prestifilippoco/spmRun/results/run_* | head -n 1)"
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
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=64 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```

Controllo:

```bash
RUN_DIR="$(ls -td /scratch/m.prestifilippoco/spmRun/results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## MPI strong scaling

Dataset fisso, nodi crescenti. Lo script copia l'input su `/scratch` locale dei
nodi usati prima del sorter; questa copia non entra nei tempi.

Job 1, nodi 1 e 2:

```bash
RUN_STRONG=1 RUN_WEAK=0 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
STRONG_NODES="1 2" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=64 \
TRIALS=1 VERIFY=0 \
sbatch --nodes=2 --time=00:25:00 benchmarks/slurm_mpi_scaling.sbatch
```

Job 2, nodi 4 e 8:

```bash
RUN_STRONG=1 RUN_WEAK=0 \
BENCHMARK_CASES="manySmall20M:20000000:64" \
STRONG_NODES="4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=64 \
TRIALS=1 VERIFY=0 \
sbatch --nodes=8 --time=00:25:00 benchmarks/slurm_mpi_scaling.sbatch
```

Controllo:

```bash
squeue -u "$USER"
tail -f /scratch/m.prestifilippoco/spmRun/slurm/slurm_mpi_*.out
tail -n 160 /scratch/m.prestifilippoco/spmRun/slurm/slurm_mpi_*.err
RUN_DIR="$(ls -td /scratch/m.prestifilippoco/spmRun/results/run_* | head -n 1)"
cat "$RUN_DIR/mpi_strong_summary.csv"
ls -lh "$RUN_DIR/logs"
```

Nel report interpreta questa parte con Amdahl: I/O, merge locale, merge
distribuito e comunicazione limitano lo speedup.

## MPI weak scaling

Il lavoro cresce con i nodi: `2.5M` record per nodo, quindi `20M` record a 8
nodi.

Job 1, nodi 1 e 2:

```bash
RUN_STRONG=0 RUN_WEAK=1 \
WEAK_CASES="weakSmall2500k:2500000:64" \
STRONG_NODES="1 2" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=64 \
TRIALS=1 VERIFY=0 \
sbatch --nodes=2 --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Job 2, nodi 4 e 8:

```bash
RUN_STRONG=0 RUN_WEAK=1 \
WEAK_CASES="weakSmall2500k:2500000:64" \
STRONG_NODES="4 8" \
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="4 16" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=64 \
TRIALS=1 VERIFY=0 \
sbatch --nodes=8 --time=00:20:00 benchmarks/slurm_mpi_scaling.sbatch
```

Controllo:

```bash
tail -n 160 /scratch/m.prestifilippoco/spmRun/slurm/slurm_mpi_*.err
RUN_DIR="$(ls -td /scratch/m.prestifilippoco/spmRun/results/run_* | head -n 1)"
cat "$RUN_DIR/mpi_weak_summary.csv"
```

Nel report interpreta questa parte con Gustafson e con la weak efficiency.

## Correttezza

La verifica va tenuta fuori dai benchmark finali e fatta su una run piccola.

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=128 MERGE_FAN=64 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## Varianti legacy

Le vecchie varianti pipeline e flat non sono piu' parte della campagna attiva.
Restano archiviate nelle cartelle `legacy` per consultazione storica.

## Rigenerare summary e grafici

```bash
RUN_DIR="$(ls -td /scratch/m.prestifilippoco/spmRun/results/run_* | head -n 1)"
python3 benchmarks/analyze.py --results-dir "$RUN_DIR"
```

## Tuning opzionale (Grid Search)

Per trovare la migliore combinazione di `CHUNK_MB` e `MERGE_FAN`, usa la guida
dedicata:

```text
benchmarks/GUIDA_TUNING_OPENMP.md
```

Il tuning usa solo OpenMP e propone piu' job brevi invece di una singola grid
search lunga.
