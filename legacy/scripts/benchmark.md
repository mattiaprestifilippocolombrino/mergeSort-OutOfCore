
drwxr-xr-x 3 m.prestifilippoco m.prestifilippoco  172 Jun  4 14:50 run_632763
drwxr-xr-x 3 m.prestifilippoco m.prestifilippoco  172 Jun  4 14:51 run_632764
drwxr-xr-x 3 m.prestifilippoco m.prestifilippoco  172 Jun  4 14:49 run_632765
drwxr-xr-x 3 m.prestifilippoco m.prestifilippoco   45 Jun  4 14:49 run_632766

## OpenMP single-node

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```
run_632763
[m.prestifilippoco@spmln run_632763]$ cat single_node_summary.csv
impl,merge_impl,case,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_total_s,speedup,efficiency,baseline_sort_s,sort_speedup,sort_efficiency,baseline_merge_s,merge_speedup,merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
omp,omp_multipass,manySmall50M,50000000,64,1,64,8,36,1,34.7669,34.7669,34.7669,0,14.5696,20.1973,34.7669,1,1,14.5696,1,1,20.1973,1,1,1,1,1,1,1,1
omp,omp_multipass,manySmall50M,50000000,64,2,64,8,36,1,31.0918,31.0918,31.0918,0,12.1198,18.9719,34.7669,1.11820158,0.559100792,14.5696,1.20213205,0.601066024,20.1973,1.06459026,0.532295131,1.11820158,0.559100792,1.20213205,0.601066024,1.06459026,0.532295131
omp,omp_multipass,manySmall50M,50000000,64,4,64,8,36,1,21.8303,21.8303,21.8303,0,6.80347,15.0268,34.7669,1.59259836,0.39814959,14.5696,2.14149544,0.535373861,20.1973,1.34408523,0.336021309,1.59259836,0.39814959,2.14149544,0.535373861,1.34408523,0.336021309
omp,omp_multipass,manySmall50M,50000000,64,8,64,8,36,1,19.312,19.312,19.312,0,5.05461,14.2573,34.7669,1.80027444,0.225034305,14.5696,2.88243801,0.360304752,20.1973,1.41662867,0.177078584,1.80027444,0.225034305,2.88243801,0.360304752,1.41662867,0.177078584
omp,omp_multipass,manySmall50M,50000000,64,16,64,8,36,1,18.0959,18.0959,18.0959,0,4.66263,13.4332,34.7669,1.92125841,0.12007865,14.5696,3.12476006,0.195297504,20.1973,1.50353602,0.093971001,1.92125841,0.12007865,3.12476006,0.195297504,1.50353602,0.093971001
omp,omp_multipass,manySmall50M,50000000,64,32,64,8,36,1,17.7419,17.7419,17.7419,0,4.23334,13.5085,34.7669,1.95959283,0.0612372759,14.5696,3.44163238,0.107551012,20.1973,1.4951549,0.0467235907,1.95959283,0.0612372759,3.44163238,0.107551012,1.4951549,0.046723590


Controllo:

```bash
squeue -u "$USER"
tail -f slurm_single_*.out
tail -n 120 slurm_single_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## FastFlow single-node

Eseguilo separato da OpenMP, cosi' eventuali problemi FastFlow non sporcano la
campagna OpenMP.

```bash
RUN_OMP=0 RUN_FF=1 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
RUN_TIMEOUT_SECONDS=900 \
FF_ROOT="$HOME/fastFlow" \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```
run_632764
[m.prestifilippoco@spmln run_632764]$ cat single_node_summary.csv
impl,merge_impl,case,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_total_s,speedup,efficiency,baseline_sort_s,sort_speedup,sort_efficiency,baseline_merge_s,merge_speedup,merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
ff,ff_multipass,manySmall50M,50000000,64,1,64,8,36,1,35.3752,35.3752,35.3752,0,14.3862,20.989,35.3752,1,1,14.3862,1,1,20.989,1,1,1,1,1,1,1,1
ff,ff_multipass,manySmall50M,50000000,64,2,64,8,36,1,31.7266,31.7266,31.7266,0,11.5401,20.1865,35.3752,1.11500129,0.557500646,14.3862,1.24662698,0.623313489,20.989,1.03975429,0.519877146,1.11500129,0.557500646,1.24662698,0.623313489,1.03975429,0.519877146
ff,ff_multipass,manySmall50M,50000000,64,4,64,8,36,1,22.8797,22.8797,22.8797,0,6.31531,16.5644,35.3752,1.54613915,0.386534788,14.3862,2.27798794,0.569496984,20.989,1.26711502,0.316778754,1.54613915,0.386534788,2.27798794,0.569496984,1.26711502,0.316778754
ff,ff_multipass,manySmall50M,50000000,64,8,64,8,36,1,19.2052,19.2052,19.2052,0,5.40037,13.8048,35.3752,1.84195947,0.230244934,14.3862,2.66392858,0.332991073,20.989,1.52041319,0.190051649,1.84195947,0.230244934,2.66392858,0.332991073,1.52041319,0.190051649
ff,ff_multipass,manySmall50M,50000000,64,16,64,8,36,1,18.1225,18.1225,18.1225,0,4.6743,13.4481,35.3752,1.95200441,0.122000276,14.3862,3.07772287,0.192357679,20.989,1.56074092,0.0975463077,1.95200441,0.122000276,3.07772287,0.192357679,1.56074092,0.0975463077
ff,ff_multipass,manySmall50M,50000000,64,32,64,8,36,1,18.6907,18.6907,18.6907,0,4.4681,14.2226,35.3752,1.8926632,0.0591457249,14.3862,3.21975784,0.100617432,20.989,1.47574986,0.0461171832,1.8926632,0.0591457249,3.21975784,0.100617432,1.47574986,0.0461171832


Controllo:

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
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
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```
[m.prestifilippoco@spmln run_632765]$ cat single_node_summary.csv
impl,merge_impl,case,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_total_s,speedup,efficiency,baseline_sort_s,sort_speedup,sort_efficiency,baseline_merge_s,merge_speedup,merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
omp,omp_multipass,mediumPayload8M,8000000,512,1,64,8,33,1,10.5408,10.5408,10.5408,0,4.24358,6.29722,10.5408,1,1,4.24358,1,1,6.29722,1,1,1,1,1,1,1,1
omp,omp_multipass,mediumPayload8M,8000000,512,8,64,8,33,1,5.98966,5.98966,5.98966,0,1.88796,4.10162,10.5408,1.75983278,0.219979097,4.24358,2.24770652,0.280963315,6.29722,1.53530069,0.191912586,1.75983278,0.219979097,2.24770652,0.280963315,1.53530069,0.191912586
omp,omp_multipass,mediumPayload8M,8000000,512,32,64,8,33,1,5.89822,5.89822,5.89822,0,1.75018,4.14797,10.5408,1.78711543,0.0558473573,4.24358,2.42465346,0.0757704208,6.29722,1.51814502,0.0474420319,1.78711543,0.0558473573,2.42465346,0.0757704208,1.51814502,0.0474420319
omp,omp_multipass,largePayload2M,2000000,2048,1,64,8,32,1,7.65148,7.65148,7.65148,0,2.97106,4.68037,7.65148,1,1,2.97106,1,1,4.68037,1,1,1,1,1,1,1,1
omp,omp_multipass,largePayload2M,2000000,2048,8,64,8,32,1,4.56556,4.56556,4.56556,0,1.5159,3.04959,7.65148,1.6759127,0.209489088,2.97106,1.95993139,0.244991424,4.68037,1.53475385,0.191844232,1.6759127,0.209489088,1.95993139,0.244991424,1.53475385,0.191844232
omp,omp_multipass,largePayload2M,2000000,2048,32,64,8,32,1,4.4283,4.4283,4.4283,0,1.36469,3.06357,7.65148,1.72785945,0.0539956078,2.97106,2.17709516,0.0680342239,4.68037,1.5277503,0.047742197,1.72785945,0.0539956078,2.17709516,0.0680342239,1.5277503,0.047742197

Controllo:

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/single_node_summary.csv"
ls -lh "$RUN_DIR/logs"
```

## MPI strong scaling

Dataset fisso, nodi crescenti. Lo script copia l'input su `/scratch` locale dei
nodi usati prima del sorter; questa copia non entra nei tempi.

Job finali strong, uno per punto della curva:

```bash
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
Submitted batch job 632767
Submitted batch job 632768
Submitted batch job 632769
Submitted batch job 632770
Submitted batch job 632771
Submitted batch job 632772
Submitted batch job 632773
Submitted batch job 632774
Submitted batch job 632775
Submitted batch job 632776
Submitted batch job 632777
Submitted batch job 632778
Submitted batch job 632779
Submitted batch job 632780
Submitted batch job 632781
Submitted batch job 632782
Submitted batch job 632783
Submitted batch job 632784
Submitted batch job 632785
Submitted batch job 632786

632767
case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall50M,50000000,64,1,1,1,1,1,64,8,mpi_local_multipass,36,1,34.597,34.597,34.597,0,34.5969,1.354e-06,1,1,1,1,1,1,1,1,1,1,1,1,1

632768
manySmall50M,50000000,64,1,1,1,4,4,64,8,mpi_local_multipass,36,1,28.3248,28.3248,28.3248,0,28.3247,1.326e-06,1,1,1,1,1,1,1,1,1,1,1,1,1

632769
manySmall50M,50000000,64,1,1,1,8,8,64,8,mpi_local_multipass,36,1,22.1163,22.1163,22.1163,0,22.1162,1.346e-06,1,1,1,1,1,1,1,1,1,1,1,1,1

632770
manySmall50M,50000000,64,1,1,1,16,16,64,8,mpi_local_multipass,36,1,21.5771,21.5771,21.5771,0,21.577,1.382e-06,1,1,1,1,1,1,1,1,1,1,1,1,1

632771
manySmall50M,50000000,64,1,1,1,32,32,64,8,mpi_local_multipass,36,1,21.1301,21.1301,21.1301,0,21.13,2.049e-06,1,1,1,1,1,1,1,1,1,1,1,1,1

632772
manySmall50M,50000000,64,2,2,1,1,2,64,8,mpi_local_multipass,36,1,29.7274,29.7274,29.7274,0,20.6951,9.03225,2,1,1,1,1,1,1,1,1,1,1,1,1

Submitted batch job 632773
manySmall50M,50000000,64,2,2,1,4,8,64,8,mpi_local_multipass,36,1,28.6892,28.6892,28.6892,0,19.6607,9.02833,2,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632774
manySmall50M,50000000,64,2,2,1,8,16,64,8,mpi_local_multipass,36,1,26.283,26.283,26.283,0,17.1975,9.08548,2,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632775
manySmall50M,50000000,64,2,2,1,16,32,64,8,mpi_local_multipass,36,1,25.6902,25.6902,25.6902,0,16.6991,8.991,2,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632776
manySmall50M,50000000,64,2,2,1,32,64,64,8,mpi_local_multipass,36,1,30.191,30.191,30.191,0,18.8886,11.3023,2,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632777
manySmall50M,50000000,64,4,4,1,1,4,64,8,mpi_local_multipass,36,1,32.0952,32.0952,32.0952,0,17.1538,14.9413,4,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632778
manySmall50M,50000000,64,4,4,1,4,16,64,8,mpi_local_multipass,36,1,32.4462,32.4462,32.4462,0,15.5826,16.8634,4,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632779
manySmall50M,50000000,64,4,4,1,8,32,64,8,mpi_local_multipass,36,1,28.633,28.633,28.633,0,15.2125,13.4204,4,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632780
manySmall50M,50000000,64,4,4,1,16,64,64,8,mpi_local_multipass,36,1,31.9927,31.9927,31.9927,0,15.1321,16.8605,4,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632781
manySmall50M,50000000,64,4,4,1,32,128,64,8,mpi_local_multipass,36,1,30.0027,30.0027,30.0027,0,15.0379,14.9646,4,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632782
manySmall50M,50000000,64,8,8,1,1,8,64,8,mpi_local_multipass,40,1,33.3568,33.3568,33.3568,0,13.6526,19.7041,8,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632783
manySmall50M,50000000,64,8,8,1,4,32,64,8,mpi_local_multipass,40,1,32.6761,32.6761,32.6761,0,12.7435,19.9325,8,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632784
manySmall50M,50000000,64,8,8,1,8,64,64,8,mpi_local_multipass,40,1,33.6174,33.6174,33.6174,0,12.8689,20.7483,8,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632785
manySmall50M,50000000,64,8,8,1,16,128,64,8,mpi_local_multipass,40,1,33.0264,33.0264,33.0264,0,12.7175,20.3088,8,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632786
manySmall50M,50000000,64,8,8,1,32,256,64,8,mpi_local_multipass,40,1,32.8168,32.8168,32.8168,0,12.7502,20.0664,8,1,1,1,1,1,1,1,1,1,1,1,1

scp -i "$HOME\.ssh\id_ed25519" -r m.prestifilippoco@spmcluster.unipi.it:/home/m.prestifilippoco/spmProject/benchmark_results "C:\Users\matti\Downloads\"

In alternativa, sottometti strong e weak insieme con l'helper finale:

```bash
RANKS_PER_NODE=1 \
MPI_THREAD_LIST="1 4 8 16 32" \
./benchmarks/submit_final_mpi_jobs.sh
```

L'helper usa job separati per ogni coppia `(nodi, thread/rank)`: `00:29:00`
per MPI strong e `00:03:00` per MPI weak, mantenendo identici `CHUNK_MB` e
`MERGE_FAN`.

Controllo:

```bash
squeue -u "$USER"
tail -f slurm_mpi_*.out
tail -n 160 slurm_mpi_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/mpi_strong_summary.csv"
ls -lh "$RUN_DIR/logs"
```

Nel report interpreta questa parte con Amdahl: I/O, merge locale, merge
distribuito e comunicazione limitano lo speedup.

## MPI weak capacity

La weak finale non passa piu' una dimensione statica del dataset. Per ogni
coppia `(nodi, thread/rank)` lo script genera una sonda interna derivata da
`CHUNK_MB`, `MERGE_FAN` e `WEAK_PROBE_CHUNKS_PER_RANK`, misura il throughput e
lo normalizza su `WEAK_TIME_BUDGET_SECONDS=180`. Il risultato da usare nel
report e' quanti GiB vengono processati in 3 minuti per nodo e in totale.

Job finali weak, uno per coppia `(nodi, thread/rank)`:

```bash
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
Submitted batch job 632787
case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,11184810,64,1,1,1,1,1,11184810,64,8,8,mpi_local_multipass,8,1,5.8624,5.8624,5.8624,0,5.86232,1.413e-06,536860963,0.499990734,0.499990734,180,15.3517897,15.3517897,0.0852877208,0.0852877208,1,15.3517897,1,15.3517897,1,1,1,1,1,1,1,1,1,1,1,1,1

Submitted batch job 632788
weak_capacity_p64_c64_f8_probe8,11184810,64,1,1,1,4,4,11184810,64,8,8,mpi_local_multipass,8,1,4.03157,4.03157,4.03157,0,4.03149,1.355e-06,536860963,0.499990734,0.499990734,180,22.3233956,22.3233956,0.124018865,0.124018865,1,22.3233956,1,22.3233956,1,1,1,1,1,1,1,1,1,1,1,1,1

Submitted batch job 632789
Submitted batch job 632790
Submitted batch job 632791
Submitted batch job 632792
Submitted batch job 632793
Submitted batch job 632794
Submitted batch job 632795
Submitted batch job 632796
Submitted batch job 632797
Submitted batch job 632798
Submitted batch job 632799
Submitted batch job 632800
Submitted batch job 632801
Submitted batch job 632802
Submitted batch job 632803
Submitted batch job 632804
Submitted batch job 632805
weak_capacity_p64_c64_f8_probe8,89478480,64,8,8,1,16,128,11184810,64,8,8,mpi_local_multipass,72,1,60.7291,60.7291,60.7291,0,24.6359,36.0931,4.29506622e+09,4.00009213,0.500011516,180,11.8562037,1.48202547,0.0658677986,0.00823347483,8,11.8562037,1,1.48202547,1,1,1,1,1,1,1,1,1,1,1,1,1
Submitted batch job 632806
_probe8,89478480,64,8,8,1,32,256,11184810,64,8,8,mpi_local_multipass,72,1,59.8623,59.8623,59.8623,0,24.6289,35.2333,4.29506622e+09,4.00009213,0.500011516,180,12.0278804,1.50348505,0.0668215576,0.0083526947,8,12.0278804,1,1.50348505,1,1,1,1,1,1,1,1,1,1,1,1,1



Controllo:

```bash
tail -n 160 slurm_mpi_*.err
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
cat "$RUN_DIR/mpi_weak_summary.csv"
```

Nel report interpreta questa parte con Gustafson e con la weak efficiency.

## Correttezza

La verifica va tenuta fuori dai benchmark finali e fatta su una run piccola.

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="check:1000000:64" \
THREAD_LIST="1 8" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=1 \
sbatch --time=00:10:00 benchmarks/slurm_single_node.sbatch
```

## Varianti legacy

Le vecchie varianti pipeline e flat non sono piu' parte della campagna attiva.
Restano archiviate nelle cartelle `legacy` per consultazione storica.

## Rigenerare summary e grafici

```bash
RUN_DIR="$(ls -td benchmark_results/run_* | head -n 1)"
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
