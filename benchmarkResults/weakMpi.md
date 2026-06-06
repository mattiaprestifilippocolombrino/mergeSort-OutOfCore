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


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,11184810,64,1,1,1,1,1,11184810,64,8,8,mpi_local_multipass,8,1,5.95718,5.95718,5.95718,0,5.9571,2.037e-06,536860963,0.499990734,0.499990734,180,15.1075395,15.1075395,0.083930775,0.083930775,1,15.1075395,1,15.1075395,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 1
  threads  : 1
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633250/work

Input bytes : 536860963
Stripe bytes: r0=536860963
Fase 1 (sort locale): 8 run create in 5.9571 s
Fase 2 (merge distribuito): 2.037e-06 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 5.9571 s
  Merge dist.  (Fase 2) : 2.037e-06 s
  Totale               : 5.95718 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,11184810,64,1,1,1,32,32,11184810,64,8,8,mpi_local_omp_multipass,8,1,3.2354,3.2354,3.2354,0,3.23534,1.953e-06,536860963,0.499990734,0.499990734,180,27.8167559,27.8167559,0.154537533,0.154537533,1,27.8167559,1,27.8167559,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 1
  threads  : 32
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633254/work

Input bytes : 536860963
Stripe bytes: r0=536860963
Fase 1 (sort locale): 8 run create in 3.23534 s
Fase 2 (merge distribuito): 1.953e-06 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 3.23534 s
  Merge dist.  (Fase 2) : 1.953e-06 s
  Totale               : 3.2354 s




case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,22369620,64,2,2,1,1,2,11184810,64,8,8,mpi_local_multipass,18,1,14.3865,14.3865,14.3865,0,10.2605,4.12586,1.07375442e+09,1.00001173,0.500005864,180,12.5118765,6.25593824,0.0695104249,0.0347552124,2,12.5118765,1,6.25593824,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 1
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633255/work

Input bytes : 1073754416
Stripe bytes: r0=536877220 r1=536877196
Fase 1 (sort locale): 18 run create in 10.2605 s
Fase 2 (merge distribuito): 4.12586 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 10.2605 s
  Merge dist.  (Fase 2) : 4.12586 s
  Totale               : 14.3865 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,22369620,64,2,2,1,4,8,11184810,64,8,8,mpi_local_omp_multipass,18,1,11.8297,11.8297,11.8297,0,7.72913,4.10045,1.07375442e+09,1.00001173,0.500005864,180,15.216118,7.60805899,0.0845339888,0.0422669944,2,15.216118,1,7.60805899,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 4
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633256/work

Input bytes : 1073754416
Stripe bytes: r0=536877220 r1=536877196
Fase 1 (sort locale): 18 run create in 7.72913 s
Fase 2 (merge distribuito): 4.10045 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 7.72913 s
  Merge dist.  (Fase 2) : 4.10045 s
  Totale               : 11.8297 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,22369620,64,2,2,1,8,16,11184810,64,8,8,mpi_local_omp_multipass,18,1,11.7902,11.7902,11.7902,0,7.58973,4.20038,1.07375442e+09,1.00001173,0.500005864,180,15.2670956,7.63354782,0.0848171979,0.042408599,2,15.2670956,1,7.63354782,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 8
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633257/work

Input bytes : 1073754416
Stripe bytes: r0=536877220 r1=536877196
Fase 1 (sort locale): 18 run create in 7.58973 s
Fase 2 (merge distribuito): 4.20038 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 7.58973 s
  Merge dist.  (Fase 2) : 4.20038 s
  Totale               : 11.7902 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,22369620,64,2,2,1,16,32,11184810,64,8,8,mpi_local_omp_multipass,18,1,11.5913,11.5913,11.5913,0,7.47319,4.118,1.07375442e+09,1.00001173,0.500005864,180,15.5290702,7.76453508,0.086272612,0.043136306,2,15.5290702,1,7.76453508,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 16
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633258/work

Input bytes : 1073754416
Stripe bytes: r0=536877220 r1=536877196
Fase 1 (sort locale): 18 run create in 7.47319 s
Fase 2 (merge distribuito): 4.118 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 7.47319 s
  Merge dist.  (Fase 2) : 4.118 s
  Totale               : 11.5913 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,22369620,64,2,2,1,32,64,11184810,64,8,8,mpi_local_omp_multipass,18,1,12.7807,12.7807,12.7807,0,8.54317,4.23741,1.07375442e+09,1.00001173,0.500005864,180,14.0839008,7.0419504,0.0782438933,0.0391219467,2,14.0839008,1,7.0419504,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 2
  threads  : 32
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633259/work

Input bytes : 1073754416
Stripe bytes: r0=536877220 r1=536877196
Fase 1 (sort locale): 18 run create in 8.54317 s
Fase 2 (merge distribuito): 4.23741 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 8.54317 s
  Merge dist.  (Fase 2) : 4.23741 s
  Totale               : 12.7807 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,44739240,64,4,4,1,1,4,11184810,64,8,8,mpi_local_multipass,36,1,31.8879,31.8879,31.8879,0,15.7008,16.1869,2.14755117e+09,2.00006289,0.500015722,180,11.2899037,2.82247592,0.0627216872,0.0156804218,4,11.2899037,1,2.82247592,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 1
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633260/work

Input bytes : 2147551174
Stripe bytes: r0=536887819 r1=536887785 r2=536887787 r3=536887783
Fase 1 (sort locale): 36 run create in 15.7008 s
Fase 2 (merge distribuito): 16.1869 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 15.7008 s
  Merge dist.  (Fase 2) : 16.1869 s
  Totale               : 31.8879 s

case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,44739240,64,4,4,1,4,16,11184810,64,8,8,mpi_local_omp_multipass,36,1,31.4115,31.4115,31.4115,0,14.1924,17.2189,2.14755117e+09,2.00006289,0.500015722,180,11.4611311,2.86528278,0.0636729506,0.0159182377,4,11.4611311,1,2.86528278,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 4
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633261/work

Input bytes : 2147551174
Stripe bytes: r0=536887819 r1=536887785 r2=536887787 r3=536887783
Fase 1 (sort locale): 36 run create in 14.1924 s
Fase 2 (merge distribuito): 17.2189 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 14.1924 s
  Merge dist.  (Fase 2) : 17.2189 s
  Totale               : 31.4115 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,44739240,64,4,4,1,8,32,11184810,64,8,8,mpi_local_omp_multipass,36,1,30.6687,30.6687,30.6687,0,13.7992,16.8693,2.14755117e+09,2.00006289,0.500015722,180,11.7387212,2.93468031,0.065215118,0.0163037795,4,11.7387212,1,2.93468031,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 8
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633262/work

Input bytes : 2147551174
Stripe bytes: r0=536887819 r1=536887785 r2=536887787 r3=536887783
Fase 1 (sort locale): 36 run create in 13.7992 s
Fase 2 (merge distribuito): 16.8693 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 13.7992 s
  Merge dist.  (Fase 2) : 16.8693 s
  Totale               : 30.6687 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,44739240,64,4,4,1,16,64,11184810,64,8,8,mpi_local_omp_multipass,36,1,29.8278,29.8278,29.8278,0,13.9777,15.8499,2.14755117e+09,2.00006289,0.500015722,180,12.0696572,3.01741429,0.0670536509,0.0167634127,4,12.0696572,1,3.01741429,1,1,1,1,1,1,1,1,1,1,1,1,1

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 16
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633263/work

Input bytes : 2147551174
Stripe bytes: r0=536887819 r1=536887785 r2=536887787 r3=536887783
Fase 1 (sort locale): 36 run create in 13.9777 s
Fase 2 (merge distribuito): 15.8499 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 13.9777 s
  Merge dist.  (Fase 2) : 15.8499 s
  Totale               : 29.8278 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,44739240,64,4,4,1,32,128,11184810,64,8,8,mpi_local_omp_multipass,36,1,29.7294,29.7294,29.7294,0,13.9456,15.7836,2.14755117e+09,2.00006289,0.500015722,180,12.109606,3.02740149,0.0672755888,0.0168188972,4,12.109606,1,3.02740149,1,1,1,1,1,1,1,1,1,1,1,1,1

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 4
  threads  : 32
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633264/work

Input bytes : 2147551174
Stripe bytes: r0=536887819 r1=536887785 r2=536887787 r3=536887783
Fase 1 (sort locale): 36 run create in 13.9456 s
Fase 2 (merge distribuito): 15.7836 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 13.9456 s
  Merge dist.  (Fase 2) : 15.7836 s
  Totale               : 29.7294 s



case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,89478480,64,8,8,1,1,8,11184810,64,8,8,mpi_local_multipass,72,1,62.7621,62.7621,62.7621,0,26.8915,35.8705,4.29506622e+09,4.00009213,0.500011516,180,11.4721557,1.43401946,0.0637341983,0.00796677479,8,11.4721557,1,1.43401946,1,1,1,1,1,1,1,1,1,1,1,1,1

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 1
  chunk    : 64 MB
  local merge : mpi-local-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633265/work

Input bytes : 4295066218
Stripe bytes: r0=536883325 r1=536883255 r2=536883278 r3=536883258 r4=536883293 r5=536883300 r6=536883293 r7=536883216
Fase 1 (sort locale): 72 run create in 26.8915 s
Fase 2 (merge distribuito): 35.8705 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 26.8915 s
  Merge dist.  (Fase 2) : 35.8705 s
  Totale               : 62.7621 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,89478480,64,8,8,1,4,32,11184810,64,8,8,mpi_local_omp_multipass,72,1,63.5975,63.5975,63.5975,0,24.9131,38.6844,4.29506622e+09,4.00009213,0.500011516,180,11.3214605,1.41518256,0.0628970027,0.00786212534,8,11.3214605,1,1.41518256,1,1,1,1,1,1,1,1,1,1,1,1,1

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 4
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633266/work

Input bytes : 4295066218
Stripe bytes: r0=536883325 r1=536883255 r2=536883278 r3=536883258 r4=536883293 r5=536883300 r6=536883293 r7=536883216
Fase 1 (sort locale): 72 run create in 24.9131 s
Fase 2 (merge distribuito): 38.6844 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 24.9131 s
  Merge dist.  (Fase 2) : 38.6844 s
  Totale               : 63.5975 s



case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,89478480,64,8,8,1,8,64,11184810,64,8,8,mpi_local_omp_multipass,72,1,60.8656,60.8656,60.8656,0,24.6028,36.259,4.29506622e+09,4.00009213,0.500011516,180,11.8296145,1.47870181,0.0657200804,0.00821501006,8,11.8296145,1,1.47870181,1,1,1,1,1,1,1,1,1,1,1,1,1


=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 8
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633267/work

Input bytes : 4295066218
Stripe bytes: r0=536883325 r1=536883255 r2=536883278 r3=536883258 r4=536883293 r5=536883300 r6=536883293 r7=536883216
Fase 1 (sort locale): 72 run create in 24.6028 s
Fase 2 (merge distribuito): 36.259 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 24.6028 s
  Merge dist.  (Fase 2) : 36.259 s
  Totale               : 60.8656 s



case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,89478480,64,8,8,1,16,128,11184810,64,8,8,mpi_local_omp_multipass,72,1,60.5778,60.5778,60.5778,0,24.7534,35.8243,4.29506622e+09,4.00009213,0.500011516,180,11.885816,1.485727,0.066032311,0.00825403887,8,11.885816,1,1.485727,1,1,1,1,1,1,1,1,1,1,1,1,1

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 16
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633268/work

Input bytes : 4295066218
Stripe bytes: r0=536883325 r1=536883255 r2=536883278 r3=536883258 r4=536883293 r5=536883300 r6=536883293 r7=536883216
Fase 1 (sort locale): 72 run create in 24.7534 s
Fase 2 (merge distribuito): 35.8243 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 24.7534 s
  Merge dist.  (Fase 2) : 35.8243 s
  Totale               : 60.5778 s


case,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,probe_chunks_per_rank,local_merge_impl,generated_runs,trials,avg_total_s,min_total_s,max_total_s,stdev_total_s,avg_sort_s,avg_merge_s,avg_input_bytes,avg_total_gib,avg_gib_per_node,avg_time_budget_s,avg_capacity_total_gib,avg_capacity_gib_per_node,avg_throughput_gib_s,avg_throughput_gib_node_s,baseline_nodes,baseline_capacity_total_gib,weak_capacity_scale,baseline_capacity_gib_per_node,weak_capacity_efficiency,weak_speedup,weak_efficiency,weak_sort_speedup,weak_sort_efficiency,weak_merge_speedup,weak_merge_efficiency,total_speedup,total_efficiency,phase1_speedup,phase1_efficiency,phase2_speedup,phase2_efficiency
weak_capacity_p64_c64_f8_probe8,89478480,64,8,8,1,32,256,11184810,64,8,8,mpi_local_omp_multipass,72,1,60.5112,60.5112,60.5112,0,24.7434,35.7677,4.29506622e+09,4.00009213,0.500011516,180,11.8988978,1.48736222,0.0661049876,0.00826312346,8,11.8988978,1,1.48736222,1,1,1,1,1,1,1,1,1,1,1,1,1

=== MPI+OMP MergeSort out-of-core ===
  ranks    : 8
  threads  : 32
  chunk    : 64 MB
  local merge : mpi-local-omp-multipass
  fan-in   : 8
  tmp base : /scratch/m.prestifilippoco/spmRun/mpi/633269/work

Input bytes : 4295066218
Stripe bytes: r0=536883325 r1=536883255 r2=536883278 r3=536883258 r4=536883293 r5=536883300 r6=536883293 r7=536883216
Fase 1 (sort locale): 72 run create in 24.7434 s
Fase 2 (merge distribuito): 35.7677 s

--- Riepilogo tempi (rank 0) ---
  Sort locale  (Fase 1) : 24.7434 s
  Merge dist.  (Fase 2) : 35.7677 s
  Totale               : 60.5112 s
