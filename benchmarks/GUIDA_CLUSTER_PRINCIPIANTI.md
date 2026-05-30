# Guida principianti per i benchmark sullo spmcluster

Questa guida dice cosa fare sul cluster, quali comandi lanciare e quali file controllare. Il progetto viene portato sul cluster con GitHub.

Useremo sempre:

```bash
CHUNK_MB=64
MERGE_FAN=8
PAYLOAD_MAX_BUILD=4096
TRIALS=1
VERIFY=0
```

`MERGE_FAN` resta per compatibilita' e per le run legacy; con il merge flat nuovo non influenza la campagna finale.

## 1. Entrare nel cluster

Da PowerShell:

```powershell
ssh LOGIN@spmcluster.unipi.it
```

Se sei fuori dalla rete UNIPI, attiva prima la VPN.

## 2. Scaricare o aggiornare il progetto

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

## 3. Controlli iniziali

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

Se manca FastFlow:

```bash
cd ~
git clone https://github.com/fastflow/fastflow.git fastFlow
cd ~/spmProject
```

Nei job FastFlow usa sempre:

```bash
FF_ROOT="$HOME/fastFlow"
```

## 4. Smoke test

Serve solo a vedere che tutto compili e parta.

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
squeue -u $USER
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
```

Se va tutto bene, passa ai benchmark veri.

## 5. OpenMP single-node

Questo e' il benchmark principale per speedup ed efficiency su un nodo.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

File da guardare:

```bash
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
ls -lh benchmark_results/omp_manySmall50M_*.log
```

Nel CSV guarda:

```text
threads, avg_total_s, avg_sort_s, avg_merge_s, baseline_total_s, speedup, efficiency
```

## 6. FastFlow single-node

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
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

File da guardare:

```bash
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
ls -lh benchmark_results/ff_manySmall50M_*.log
tail -n 80 benchmark_results/ff_manySmall50M_*.log
```

Se trovi `timeout`, `pthread_create` o `spawning worker thread`, quella run non e' valida: conserva il log e commentalo.

## 7. Payload distribution

Questo job varia `N` e payload.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="mediumPayload8M:8000000:512 largePayload2M:2000000:2048" \
THREAD_LIST="1 8 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
APPEND_RESULTS=1 \
sbatch --time=00:20:00 benchmarks/slurm_single_node.sbatch
```

File da guardare:

```bash
tail -n 120 slurm_single_*.err
cat benchmark_results/single_node_summary.csv
ls -lh benchmark_results/omp_mediumPayload8M*.log benchmark_results/omp_largePayload2M*.log
```

## 8. MPI strong scaling

Dataset fisso, nodi crescenti. Serve per le curve strong scaling.

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

File da guardare:

```bash
squeue -u $USER
tail -f slurm_mpi_*.out
tail -n 160 slurm_mpi_*.err
cat benchmark_results/mpi_strong_summary.csv
```

Nel report collega questa parte ad Amdahl: merge, I/O e comunicazione limitano lo speedup.

## 9. MPI weak scaling

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

File da guardare:

```bash
tail -n 160 slurm_mpi_*.err
cat benchmark_results/mpi_weak_summary.csv
```

Nel report collega questa parte a Gustafson e alla weak efficiency:

```text
weak_efficiency = T_base / T_p
```

## 10. Controllo correttezza finale

Run piccola con `VERIFY=1`.

```bash
cd ~/spmProject
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## 11. Rigenerare summary e grafici

Dopo ogni job puoi rigenerare i summary:

```bash
cd ~/spmProject
python3 benchmarks/analyze.py --results-dir benchmark_results
```

File importanti:

```text
benchmark_results/single_node_summary.csv
benchmark_results/mpi_strong_summary.csv
benchmark_results/mpi_weak_summary.csv
benchmark_results/*.log
slurm_single_*.out
slurm_single_*.err
slurm_mpi_*.out
slurm_mpi_*.err
```

## 12. Scaricare i risultati su Windows

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

## 13. Cosa scrivere nella relazione

Single-node:

```text
speedup = Tseq / Tpar
efficiency = speedup / threads
```

Strong scaling:

```text
strong_speedup = T_base / T_p
strong_efficiency = strong_speedup / (nodes / baseline_nodes)
```

Weak scaling:

```text
weak_efficiency = T_base / T_p
```

Commenta:

- il merge e l'I/O come colli di bottiglia;
- il sort che scala meglio del merge;
- payload piccoli vs payload grandi;
- Amdahl per strong scaling;
- Gustafson per weak scaling;
- eventuali errori FastFlow, se compaiono nei log.
