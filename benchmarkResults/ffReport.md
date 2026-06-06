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

=== FastFlow MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632764/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/ff_manySmall50M_t1_i1.bin
  chunk        : 64 MB
  workers      : 1
  merge impl   : simple multi-pass
  merge fan-in : 8
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/spm_ff_4155011_21443914510191152_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort FF): 36 run create in 14.3862 s
Fase 2 (merge): 20.989 s

--- Riepilogo tempi ---
  Sort FF (Fase 1)    : 14.3862 s
  K-way merge (Fase 2): 20.989 s
  Totale              : 35.3752 s


=== FastFlow MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632764/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/ff_manySmall50M_t2_i1.bin
  chunk        : 64 MB
  workers      : 2
  merge impl   : simple multi-pass
  merge fan-in : 8
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/spm_ff_4155051_21443950601259021_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort FF): 36 run create in 11.5401 s
Fase 2 (merge): 20.1865 s

--- Riepilogo tempi ---
  Sort FF (Fase 1)    : 11.5401 s
  K-way merge (Fase 2): 20.1865 s
  Totale              : 31.7266 s


=== FastFlow MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632764/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/ff_manySmall50M_t4_i1.bin
  chunk        : 64 MB
  workers      : 4
  merge impl   : simple multi-pass
  merge fan-in : 8
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/spm_ff_4155105_21443983038973495_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort FF): 36 run create in 6.31531 s
Fase 2 (merge): 16.5644 s

--- Riepilogo tempi ---
  Sort FF (Fase 1)    : 6.31531 s
  K-way merge (Fase 2): 16.5644 s
  Totale              : 22.8797 s

=== FastFlow MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632764/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/ff_manySmall50M_t8_i1.bin
  chunk        : 64 MB
  workers      : 8
  merge impl   : simple multi-pass
  merge fan-in : 8
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/spm_ff_4155172_21444006678830978_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort FF): 36 run create in 5.40037 s
Fase 2 (merge): 13.8048 s

--- Riepilogo tempi ---
  Sort FF (Fase 1)    : 5.40037 s
  K-way merge (Fase 2): 13.8048 s
  Totale              : 19.2052 s


=== FastFlow MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632764/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/ff_manySmall50M_t16_i1.bin
  chunk        : 64 MB
  workers      : 16
  merge impl   : simple multi-pass
  merge fan-in : 8
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/spm_ff_4155248_21444026633920509_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort FF): 36 run create in 4.6743 s
Fase 2 (merge): 13.4481 s

--- Riepilogo tempi ---
  Sort FF (Fase 1)    : 4.6743 s
  K-way merge (Fase 2): 13.4481 s
  Totale              : 18.1225 s


=== FastFlow MergeSort out-of-core ===
  input        : /scratch/m.prestifilippoco/spmRun/single/632764/data/manySmall50M_n50000000_p64.bin
  output       : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/ff_manySmall50M_t32_i1.bin
  chunk        : 64 MB
  workers      : 32
  merge impl   : simple multi-pass
  merge fan-in : 8
  tmp          : /scratch/m.prestifilippoco/spmRun/single/632764/work/spm_single_node_bench_632764/spm_ff_4155337_21444045478995902_0
  PAYLOAD_MAX  : 4096 B

Fase 1 (sort FF): 36 run create in 4.4681 s
Fase 2 (merge): 14.2226 s

--- Riepilogo tempi ---
  Sort FF (Fase 1)    : 4.4681 s
  K-way merge (Fase 2): 14.2226 s
  Totale              : 18.6907 s


impl,merge_impl,case,records,payload_max,threads,chunk_mb,merge_fan,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_total_s,speedup,efficiency,baseline_sort_s,sort_speedup,sort_efficiency,baseline_merge_s,merge_speedup,merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
ff,ff_multipass,manySmall50M,50000000,64,1,64,8,36,1,35.3752,35.3752,35.3752,0,14.3862,20.989,35.3752,1,1,14.3862,1,1,20.989,1,1,1,1,1,1,1,1
ff,ff_multipass,manySmall50M,50000000,64,2,64,8,36,1,31.7266,31.7266,31.7266,0,11.5401,20.1865,35.3752,1.11500129,0.557500646,14.3862,1.24662698,0.623313489,20.989,1.03975429,0.519877146,1.11500129,0.557500646,1.24662698,0.623313489,1.03975429,0.519877146
ff,ff_multipass,manySmall50M,50000000,64,4,64,8,36,1,22.8797,22.8797,22.8797,0,6.31531,16.5644,35.3752,1.54613915,0.386534788,14.3862,2.27798794,0.569496984,20.989,1.26711502,0.316778754,1.54613915,0.386534788,2.27798794,0.569496984,1.26711502,0.316778754
ff,ff_multipass,manySmall50M,50000000,64,8,64,8,36,1,19.2052,19.2052,19.2052,0,5.40037,13.8048,35.3752,1.84195947,0.230244934,14.3862,2.66392858,0.332991073,20.989,1.52041319,0.190051649,1.84195947,0.230244934,2.66392858,0.332991073,1.52041319,0.190051649
ff,ff_multipass,manySmall50M,50000000,64,16,64,8,36,1,18.1225,18.1225,18.1225,0,4.6743,13.4481,35.3752,1.95200441,0.122000276,14.3862,3.07772287,0.192357679,20.989,1.56074092,0.0975463077,1.95200441,0.122000276,3.07772287,0.192357679,1.56074092,0.0975463077
ff,ff_multipass,manySmall50M,50000000,64,32,64,8,36,1,18.6907,18.6907,18.6907,0,4.4681,14.2226,35.3752,1.8926632,0.0591457249,14.3862,3.21975784,0.100617432,20.989,1.47574986,0.0461171832,1.8926632,0.0591457249,3.21975784,0.100617432,1.47574986,0.0461171832