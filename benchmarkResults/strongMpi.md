## MPI strong scaling

Dataset fisso, nodi crescenti. Lo script copia l'input su `/scratch` locale dei
nodi usati prima del sorter; questa copia non entra nei tempi.

Job finali strong, uno per punto della curva:

```bash
for n in 1 2 4 8; do
  for t in 1 4 8 16 32; do
    RUN_STRONG=1 RUN_WEAK=0 \
    BENCHMARK_CASES="manySmall200M:200000000:64" \
    STRONG_NODES="$n" \
    RANKS_PER_NODE=1 \
    MPI_THREAD_LIST="$t" \
    PAYLOAD_MAX_BUILD=4096 CHUNK_MB=64 MERGE_FAN=8 \
    TRIALS=1 VERIFY=0 \
    sbatch --nodes="$n" --time=00:29:00 benchmarks/slurm_mpi_scaling.sbatch
  done
done
```
=== MPI+OMP MergeSort out-of-core ===
  ranks    : 1
  threads  : 1
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633221/work

Input bytes : 9600279582
Stripe bytes: r0=9600279582
Fase 1 (sort locale): 144 run create in 179.138 s
Fase 2 (merge distribuito): 1.311e-06 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 179.138 s
  Merge dist.  (Fase 2) : 1.311e-06 s
  Totale               : 179.138 s

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 1
  threads  : 4
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633222/work

Input bytes : 9600279582
Stripe bytes: r0=9600279582
Fase 1 (sort locale): 144 run create in 153.375 s
Fase 2 (merge distribuito): 1.401e-06 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 153.375 s
  Merge dist.  (Fase 2) : 1.401e-06 s
  Totale               : 153.375 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 1
  threads  : 16
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633224/work

Input bytes : 9600279582
Stripe bytes: r0=9600279582
Fase 1 (sort locale): 144 run create in 116.737 s
Fase 2 (merge distribuito): 1.851e-06 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 116.737 s
  Merge dist.  (Fase 2) : 1.851e-06 s
  Totale               : 116.737 s

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 1
  threads  : 32
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633225/work

Input bytes : 9600279582
Stripe bytes: r0=9600279582
Fase 1 (sort locale): 144 run create in 117.376 s
Fase 2 (merge distribuito): 1.602e-06 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 117.376 s
  Merge dist.  (Fase 2) : 1.602e-06 s
  Totale               : 117.376 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 1
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633226/work

Input bytes : 9600279582
Stripe bytes: r0=4800139809 r1=4800139773
Fase 1 (sort locale): 144 run create in 97.2189 s
Fase 2 (merge distribuito): 35.7516 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 97.2189 s
  Merge dist.  (Fase 2) : 35.7516 s
  Totale               : 132.971 s

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 4
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633227/work

Input bytes : 9600279582
Stripe bytes: r0=4800139809 r1=4800139773
Fase 1 (sort locale): 144 run create in 95.4894 s
Fase 2 (merge distribuito): 35.8532 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 95.4894 s
  Merge dist.  (Fase 2) : 35.8532 s
  Totale               : 131.343 s

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 8
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633228/work

Input bytes : 9600279582
Stripe bytes: r0=4800139809 r1=4800139773
Fase 1 (sort locale): 144 run create in 69.9098 s
Fase 2 (merge distribuito): 36.6577 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 69.9098 s
  Merge dist.  (Fase 2) : 36.6577 s
  Totale               : 106.568 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 16
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633229/work

Input bytes : 9600279582
Stripe bytes: r0=4800139809 r1=4800139773
Fase 1 (sort locale): 144 run create in 67.4886 s
Fase 2 (merge distribuito): 36.6449 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 67.4886 s
  Merge dist.  (Fase 2) : 36.6449 s
  Totale               : 104.134 s

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 32
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633230/work

Input bytes : 9600279582
Stripe bytes: r0=4800139809 r1=4800139773
Fase 1 (sort locale): 144 run create in 74.5816 s
Fase 2 (merge distribuito): 36.6764 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 74.5816 s
  Merge dist.  (Fase 2) : 36.6764 s
  Totale               : 111.258 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 1
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633231/work

Input bytes : 9600279582
Stripe bytes: r0=2400069904 r1=2400069905 r2=2400069878 r3=2400069895
Fase 1 (sort locale): 144 run create in 71.5175 s
Fase 2 (merge distribuito): 69.6173 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 71.5175 s
  Merge dist.  (Fase 2) : 69.6173 s
  Totale               : 141.135 s

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 4
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633232/work

Input bytes : 9600279582
Stripe bytes: r0=2400069904 r1=2400069905 r2=2400069878 r3=2400069895
Fase 1 (sort locale): 144 run create in 58.72 s
Fase 2 (merge distribuito): 60.6109 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 58.72 s
  Merge dist.  (Fase 2) : 60.6109 s
  Totale               : 119.331 s

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 8
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633233/work

Input bytes : 9600279582
Stripe bytes: r0=2400069904 r1=2400069905 r2=2400069878 r3=2400069895
Fase 1 (sort locale): 144 run create in 56.0202 s
Fase 2 (merge distribuito): 69.3711 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 56.0202 s
  Merge dist.  (Fase 2) : 69.3711 s
  Totale               : 125.391 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 16
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633234/work

Input bytes : 9600279582
Stripe bytes: r0=2400069904 r1=2400069905 r2=2400069878 r3=2400069895
Fase 1 (sort locale): 144 run create in 55.2044 s
Fase 2 (merge distribuito): 55.6361 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 55.2044 s
  Merge dist.  (Fase 2) : 55.6361 s
  Totale               : 110.841 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 32
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633235/work

Input bytes : 9600279582
Stripe bytes: r0=2400069904 r1=2400069905 r2=2400069878 r3=2400069895
Fase 1 (sort locale): 144 run create in 54.8811 s
Fase 2 (merge distribuito): 55.489 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 54.8811 s
  Merge dist.  (Fase 2) : 55.489 s
  Totale               : 110.37 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 1
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633236/work

Input bytes : 9600279582
Stripe bytes: r0=1200034992 r1=1200034912 r2=1200034966 r3=1200034939 r4=1200034950 r5=1200034928 r6=1200034983 r7=1200034912
Fase 1 (sort locale): 144 run create in 60.662 s
Fase 2 (merge distribuito): 80.4151 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 60.662 s
  Merge dist.  (Fase 2) : 80.4151 s
  Totale               : 141.077 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 4
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633237/work

Input bytes : 9600279582
Stripe bytes: r0=1200034992 r1=1200034912 r2=1200034966 r3=1200034939 r4=1200034950 r5=1200034928 r6=1200034983 r7=1200034912
Fase 1 (sort locale): 144 run create in 53.8473 s
Fase 2 (merge distribuito): 80.0793 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 53.8473 s
  Merge dist.  (Fase 2) : 80.0793 s
  Totale               : 133.927 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 8
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633238/work

Input bytes : 9600279582
Stripe bytes: r0=1200034992 r1=1200034912 r2=1200034966 r3=1200034939 r4=1200034950 r5=1200034928 r6=1200034983 r7=1200034912
Fase 1 (sort locale): 144 run create in 52.9462 s
Fase 2 (merge distribuito): 80.8901 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 52.9462 s
  Merge dist.  (Fase 2) : 80.8901 s
  Totale               : 133.836 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 16
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633239/work

Input bytes : 9600279582
Stripe bytes: r0=1200034992 r1=1200034912 r2=1200034966 r3=1200034939 r4=1200034950 r5=1200034928 r6=1200034983 r7=1200034912
Fase 1 (sort locale): 144 run create in 53.409 s
Fase 2 (merge distribuito): 81.0612 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 53.409 s
  Merge dist.  (Fase 2) : 81.0612 s
  Totale               : 134.47 s


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 32
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633240/work

Input bytes : 9600279582
Stripe bytes: r0=1200034992 r1=1200034912 r2=1200034966 r3=1200034939 r4=1200034950 r5=1200034928 r6=1200034983 r7=1200034912
Fase 1 (sort locale): 144 run create in 52.0328 s
Fase 2 (merge distribuito): 80.9367 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 52.0328 s
  Merge dist.  (Fase 2) : 80.9367 s
  Totale               : 132.97 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,1,1,1,1,1,64,8,mpi_local_multipass,144,1,179.138,179.138,179.138,0,179.138,1.311e-06,1,1,1,1,1,1,1,1,1,1,1,1,1

case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,1,1,1,4,4,64,8,mpi_local_omp_multipass,144,1,153.375,153.375,153.375,0,153.375,1.401e-06,1,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,1,1,1,16,16,64,8,mpi_local_omp_multipass,144,1,116.737,116.737,116.737,0,116.737,1.851e-06,1,1,1,1,1,1,1,1,1,1,1,1,1

case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,1,1,1,32,32,64,8,mpi_local_omp_multipass,144,1,117.376,117.376,117.376,0,117.376,1.602e-06,1,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,2,2,1,1,2,64,8,mpi_local_multipass,144,1,132.971,132.971,132.971,0,97.2189,35.7516,2,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,2,2,1,4,8,64,8,mpi_local_omp_multipass,144,1,131.343,131.343,131.343,0,95.4894,35.8532,2,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,2,2,1,8,16,64,8,mpi_local_omp_multipass,144,1,106.568,106.568,106.568,0,69.9098,36.6577,2,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,2,2,1,16,32,64,8,mpi_local_omp_multipass,144,1,104.134,104.134,104.134,0,67.4886,36.6449,2,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,2,2,1,32,64,64,8,mpi_local_omp_multipass,144,1,111.258,111.258,111.258,0,74.5816,36.6764,2,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,4,4,1,1,4,64,8,mpi_local_multipass,144,1,141.135,141.135,141.135,0,71.5175,69.6173,4,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,4,4,1,4,16,64,8,mpi_local_omp_multipass,144,1,119.331,119.331,119.331,0,58.72,60.6109,4,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,4,4,1,8,32,64,8,mpi_local_omp_multipass,144,1,125.391,125.391,125.391,0,56.0202,69.3711,4,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,4,4,1,16,64,64,8,mpi_local_omp_multipass,144,1,110.841,110.841,110.841,0,55.2044,55.6361,4,1,1,1,1,1,1,1,1,1,1,1,1


suite,case,trial,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,sort_s,merge_s,total_s,verified,log_file
mpi_strong,manySmall200M,1,200000000,64,4,4,1,32,128,64,8,mpi_local_omp_multipass,144,54.8811,55.489,110.37,0,/home/m.prestifilippoco/spmProject/benchmark_results/run_633235/logs/mpi_strong_manySmall200M_n4_r4_t32_i1.log


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,8,8,1,1,8,64,8,mpi_local_multipass,144,1,141.077,141.077,141.077,0,60.662,80.4151,8,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,8,8,1,4,32,64,8,mpi_local_omp_multipass,144,1,133.927,133.927,133.927,0,53.8473,80.0793,8,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,8,8,1,8,64,64,8,mpi_local_omp_multipass,144,1,133.836,133.836,133.836,0,52.9462,80.8901,8,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,8,8,1,16,128,64,8,mpi_local_omp_multipass,144,1,134.47,134.47,134.47,0,53.409,81.0612,8,1,1,1,1,1,1,1,1,1,1,1,1


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,baseline_nodes,strong_speedup,strong_efficiency,strong_sort_speedup,strong_sort_efficiency,strong_merge_speedup,strong_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
manySmall200M,200000000,64,8,8,1,32,256,64,8,mpi_local_omp_multipass,144,1,132.97,132.97,132.97,0,52.0328,80.9367,8,1,1,1,1,1,1,1,1,1,1,1,1
