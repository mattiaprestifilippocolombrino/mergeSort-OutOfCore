## Payload distribution

```bash
RUN_OMP=1 RUN_FF=0 \
BENCHMARK_CASES="mediumPayload8M:8000000:512 largePayload2M:2000000:2048" \
THREAD_LIST="1 8 32" \
PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
TRIALS=1 VERIFY=0 \
sbatch --time=00:25:00 benchmarks/slurm_single_node.sbatch
```
=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632765/data/largePayload2M_n2000000_p2048.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/omp_largePayload2M_t1_i1.bin
  chunk        : 64 MB
  threads      : 1
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/spm_omp_3684991_21443549524513930_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 32 run create in 2.97106 s
Fase 2 (merge): 4.68037 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 2.97106 s
  K-way merge   (Fase 2) : 4.68037 s
  Totale                 : 7.65148 s


=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632765/data/largePayload2M_n2000000_p2048.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/omp_largePayload2M_t8_i1.bin
  chunk        : 64 MB
  threads      : 8
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/spm_omp_3685030_21443557689798573_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 32 run create in 1.5159 s
Fase 2 (merge): 3.04959 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 1.5159 s
  K-way merge   (Fase 2) : 3.04959 s
  Totale                 : 4.56556 s

=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632765/data/largePayload2M_n2000000_p2048.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/omp_largePayload2M_t32_i1.bin
  chunk        : 64 MB
  threads      : 32
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/spm_omp_3685076_21443562785058920_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 32 run create in 1.36469 s
Fase 2 (merge): 3.06357 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 1.36469 s
  K-way merge   (Fase 2) : 3.06357 s
  Totale                 : 4.4283 s


=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632765/data/mediumPayload8M_n8000000_p512.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/omp_mediumPayload8M_t1_i1.bin
  chunk        : 64 MB
  threads      : 1
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/spm_omp_3684794_21443523176584641_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 33 run create in 4.24358 s
Fase 2 (merge): 6.29722 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 4.24358 s
  K-way merge   (Fase 2) : 6.29722 s
  Totale                 : 10.5408 s

=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632765/data/mediumPayload8M_n8000000_p512.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/omp_mediumPayload8M_t8_i1.bin
  chunk        : 64 MB
  threads      : 8
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/spm_omp_3684833_21443534261390983_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 33 run create in 1.88796 s
Fase 2 (merge): 4.10162 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 1.88796 s
  K-way merge   (Fase 2) : 4.10162 s
  Totale                 : 5.98966 s

=== OMP MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632765/data/mediumPayload8M_n8000000_p512.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/omp_mediumPayload8M_t32_i1.bin
  chunk        : 64 MB
  threads      : 32
  merge impl   : simple multi-pass
  merge fan-in : 8
  merge paral. : si
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632765/work/spm_single_node_bench_632765/spm_omp_3684880_21443540766565997_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort): 33 run create in 1.75018 s
Fase 2 (merge): 4.14797 s

--- Riepilogo tempi ---
  Sort parallelo (Fase 1) : 1.75018 s
  K-way merge   (Fase 2) : 4.14797 s
  Totale                 : 5.89822 s

impl,merge_impl,case,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_total_s,speedup,efficiency,baseline_sort_s,sort_speedup,sort_efficiency,baseline_merge_s,merge_speedup,merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
omp,omp_multipass,mediumPayload8M,8000000,512,1,64,8,33,1,10.5408,10.5408,10.5408,0,4.24358,6.29722,10.5408,1,1,4.24358,1,1,6.29722,1,1,1,1,1,1,1,1
omp,omp_multipass,mediumPayload8M,8000000,512,8,64,8,33,1,5.98966,5.98966,5.98966,0,1.88796,4.10162,10.5408,1.75983278,0.219979097,4.24358,2.24770652,0.280963315,6.29722,1.53530069,0.191912586,1.75983278,0.219979097,2.24770652,0.280963315,1.53530069,0.191912586
omp,omp_multipass,mediumPayload8M,8000000,512,32,64,8,33,1,5.89822,5.89822,5.89822,0,1.75018,4.14797,10.5408,1.78711543,0.0558473573,4.24358,2.42465346,0.0757704208,6.29722,1.51814502,0.0474420319,1.78711543,0.0558473573,2.42465346,0.0757704208,1.51814502,0.0474420319
omp,omp_multipass,largePayload2M,2000000,2048,1,64,8,32,1,7.65148,7.65148,7.65148,0,2.97106,4.68037,7.65148,1,1,2.97106,1,1,4.68037,1,1,1,1,1,1,1,1
omp,omp_multipass,largePayload2M,2000000,2048,8,64,8,32,1,4.56556,4.56556,4.56556,0,1.5159,3.04959,7.65148,1.6759127,0.209489088,2.97106,1.95993139,0.244991424,4.68037,1.53475385,0.191844232,1.6759127,0.209489088,1.95993139,0.244991424,1.53475385,0.191844232
omp,omp_multipass,largePayload2M,2000000,2048,32,64,8,32,1,4.4283,4.4283,4.4283,0,1.36469,3.06357,7.65148,1.72785945,0.0539956078,2.97106,2.17709516,0.0680342239,4.68037,1.5277503,0.047742197,1.72785945,0.0539956078,2.17709516,0.0680342239,1.5277503,0.047742197

