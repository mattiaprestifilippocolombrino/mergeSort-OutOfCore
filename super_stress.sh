#!/bin/bash

# ==============================================================================
# SUPER STRESS TEST - EFFICIENZA ESTREMA
# ==============================================================================
# Obiettivo: Massima saturazione I/O e CPU con merge multi-passata parallelo.

INPUT="/tmp/stress_input.bin"
OUTPUT="/tmp/stress_output.bin"
RECORDS=10000000        # ~2 GB di dati
CHUNK=32                # Chunk piccoli = tantissime run (60-70 run)
FANIN=4                 # Fan-in basso = molteplici passate di merge (Log scale)
THREADS=$(nproc)
TMP="/tmp"

echo "--- 1. Preparazione Ultra-Veloce (2GB) ---"
./build/generate $INPUT $RECORDS --payload-max 256

run_bench() {
    local label=$1
    local cmd=$2
    echo -e "\n\033[1;33m[STRESS] $label\033[0m"
    # Sincronizziamo il disco prima di partire per pulire la cache del kernel
    sync && sudo sh -c "echo 3 > /proc/sys/vm/drop_caches" 2>/dev/null || true
    
    /usr/bin/time -f "Tempo Reale: %e s | CPU: %P | RAM Max: %M KB" $cmd
    
    ./build/verify $INPUT $OUTPUT && echo -e "\033[1;32mRISULTATO CORRETTO\033[0m" || exit 1
    rm -f $OUTPUT
}

# --- TEST 1: OMP MULTI-PASS ---
# Testiamo il parallelismo dei task OpenMP durante il merge di molti gruppi
run_bench "OpenMP (Multi-Pass Parallel Merge)" \
    "./build/omp_sort $INPUT $OUTPUT --chunk-mb $CHUNK --threads $THREADS --merge-fan $FANIN --tmp-dir $TMP"

# --- TEST 2: FASTFLOW MULTI-PASS ---
# Testiamo il ParallelFor di FastFlow sotto carico di I/O
run_bench "FastFlow (Multi-Pass Parallel Merge)" \
    "./build/ff_sort $INPUT $OUTPUT --chunk-mb $CHUNK --workers $THREADS --merge-fan $FANIN --tmp-dir $TMP"

# --- TEST 3: MPI TREE REDUCTION ---
# Il caso più estremo: ogni rank gestisce le sue run e poi fonde via rete locale
run_bench "MPI (Aggressive Tree-Merge - 4 Rank)" \
    "mpirun --oversubscribe -n 4 ./build/mpi_sort $INPUT $OUTPUT --chunk-mb $CHUNK --threads $((THREADS/4)) --merge-fan $FANIN --tmp-dir $TMP"

echo -e "\n=== Stress Test Completato! ==="
rm -f $INPUT
