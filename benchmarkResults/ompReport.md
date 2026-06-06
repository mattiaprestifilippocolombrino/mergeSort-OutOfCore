
## OpenMP single-node

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="manySmall50M:50000000:64" \
THREAD_LIST="1 2 4 8 16 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```
=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632763/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/omp_manySmall50M_t1_i1.bin
  chunk        : 64 MB
  threads      : 1
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/spm_omp_565495_21444542902044457_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 36 run create in 14.5696 s
Fase 2 (merge): 20.1973 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 14.5696 s
  K-way merge   (Fase 2) : 20.1973 s
  Totale                 : 34.7669 s

=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632763/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/omp_manySmall50M_t2_i1.bin
  chunk        : 64 MB
  threads      : 2
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/spm_omp_565540_21444578262600377_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 36 run create in 12.1198 s
Fase 2 (merge): 18.9719 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 12.1198 s
  K-way merge   (Fase 2) : 18.9719 s
  Totale                 : 31.0918 s

=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632763/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/omp_manySmall50M_t4_i1.bin
  chunk        : 64 MB
  threads      : 4
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/spm_omp_565587_21444609960172777_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 36 run create in 6.80347 s
Fase 2 (merge): 15.0268 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 6.80347 s
  K-way merge   (Fase 2) : 15.0268 s
  Totale                 : 21.8303 s

=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632763/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/omp_manySmall50M_t8_i1.bin
  chunk        : 64 MB
  threads      : 8
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/spm_omp_565630_21444632432719637_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 36 run create in 5.05461 s
Fase 2 (merge): 14.2573 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 5.05461 s
  K-way merge   (Fase 2) : 14.2573 s
  Totale                 : 19.312 s


=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632763/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/omp_manySmall50M_t16_i1.bin
  chunk        : 64 MB
  threads      : 16
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/spm_omp_565690_21444652358356507_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 36 run create in 4.66263 s
Fase 2 (merge): 13.4332 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 4.66263 s
  K-way merge   (Fase 2) : 13.4332 s
  Totale                 : 18.0959 s


=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632763/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/omp_manySmall50M_t32_i1.bin
  chunk        : 64 MB
  threads      : 32
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632763/work/spm_single_node_bench_632763/spm_omp_565746_21444671072210125_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 36 run create in 4.23334 s
Fase 2 (merge): 13.5085 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 4.23334 s
  K-way merge   (Fase 2) : 13.5085 s
  Totale                 : 17.7419 s


impl,merge_impl,case,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_total_s,speedup,efficiency,baseline_sort_s,sort_speedup,sort_efficiency,baseline_merge_s,merge_speedup,merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
omp,omp_multipass,manySmall50M,50000000,64,1,64,8,36,1,34.7669,34.7669,34.7669,0,14.5696,20.1973,34.7669,1,1,14.5696,1,1,20.1973,1,1,1,1,1,1,1,1
omp,omp_multipass,manySmall50M,50000000,64,2,64,8,36,1,31.0918,31.0918,31.0918,0,12.1198,18.9719,34.7669,1.11820158,0.559100792,14.5696,1.20213205,0.601066024,20.1973,1.06459026,0.532295131,1.11820158,0.559100792,1.20213205,0.601066024,1.06459026,0.532295131
omp,omp_multipass,manySmall50M,50000000,64,4,64,8,36,1,21.8303,21.8303,21.8303,0,6.80347,15.0268,34.7669,1.59259836,0.39814959,14.5696,2.14149544,0.535373861,20.1973,1.34408523,0.336021309,1.59259836,0.39814959,2.14149544,0.535373861,1.34408523,0.336021309
omp,omp_multipass,manySmall50M,50000000,64,8,64,8,36,1,19.312,19.312,19.312,0,5.05461,14.2573,34.7669,1.80027444,0.225034305,14.5696,2.88243801,0.360304752,20.1973,1.41662867,0.177078584,1.80027444,0.225034305,2.88243801,0.360304752,1.41662867,0.177078584
omp,omp_multipass,manySmall50M,50000000,64,16,64,8,36,1,18.0959,18.0959,18.0959,0,4.66263,13.4332,34.7669,1.92125841,0.12007865,14.5696,3.12476006,0.195297504,20.1973,1.50353602,0.093971001,1.92125841,0.12007865,3.12476006,0.195297504,1.50353602,0.093971001
omp,omp_multipass,manySmall50M,50000000,64,32,64,8,36,1,17.7419,17.7419,17.7419,0,4.23334,13.5085,34.7669,1.95959283,0.0612372759,14.5696,3.44163238,0.107551012,20.1973,1.4951549,0.0467235907,1.95959283,0.0612372759,3.44163238,0.107551012,1.4951549,0.0467235907
