#!/bin/bash

# ==============================================================================
# SCRIPT DI BENCHMARK - MERGESORT OUT-OF-CORE
# ==============================================================================
# Questo script compila il progetto, genera un dataset di test e confronta
# le performance delle versioni OMP, FastFlow e MPI.

# Impostazioni del test
INPUT_FILE="/tmp/test_data.bin"
OUTPUT_FILE="/tmp/sorted_output.bin"
RECORDS=5000000        # Circa 1GB di dati (media 200B a record)
CHUNK_MB=128           # Dimensione chunk (influisce sul numero di run temporanee)
MERGE_FAN=16           # Fan-in multi-pass: bilancia passate, RAM e file descriptor
THREADS=$(nproc)       # Usa tutti i core disponibili
TMP_DIR="/tmp"

echo "=== 1. Compilazione del progetto ==="
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..

echo -e "\n=== 2. Generazione dataset di test ($RECORDS record) ==="
./build/generate $INPUT_FILE $RECORDS --payload-max 256

# Funzione per eseguire il test e verificare il risultato
run_test() {
    local name=$1
    local cmd=$2
    echo -e "\n>>> Test: $name"
    
    # Esecuzione con misurazione del tempo
    eval "time $cmd"
    
    # Verifica della correttezza
    echo "Verifica correttezza..."
    ./build/verify $INPUT_FILE $OUTPUT_FILE
    if [ $? -eq 0 ]; then
        echo "VERIFICA OK"
    else
        echo "VERIFICA FALLITA!"
        exit 1
    fi
    rm -f $OUTPUT_FILE
}

# --- TEST 1: OpenMP ---
run_test "OpenMP (Shared Memory Tasks + Multi-pass merge)" \
    "./build/omp_sort $INPUT_FILE $OUTPUT_FILE --chunk-mb $CHUNK_MB --threads $THREADS --tmp-dir $TMP_DIR --multipass-merge --merge-fan $MERGE_FAN"

# --- TEST 2: FastFlow ---
run_test "FastFlow (Farm + ParallelFor + Multi-pass merge)" \
    "./build/ff_sort $INPUT_FILE $OUTPUT_FILE --chunk-mb $CHUNK_MB --workers $THREADS --tmp-dir $TMP_DIR --multipass-merge --merge-fan $MERGE_FAN"

# --- TEST 3: MPI (2 Rank) ---
# Simuliamo un ambiente distribuito su macchina locale con 2 processi MPI
run_test "MPI (Distributed - 2 Processi, $((THREADS/2)) threads ciascuno, Multi-pass locale)" \
    "mpirun --oversubscribe -n 2 ./build/mpi_sort $INPUT_FILE $OUTPUT_FILE --chunk-mb $CHUNK_MB --threads $((THREADS/2)) --tmp-dir $TMP_DIR --multipass-local-merge --merge-fan $MERGE_FAN"

# --- TEST 4: MPI (4 Rank) ---
run_test "MPI (Distributed - 4 Processi, $((THREADS/4)) threads ciascuno, Multi-pass locale)" \
    "mpirun --oversubscribe -n 4 ./build/mpi_sort $INPUT_FILE $OUTPUT_FILE --chunk-mb $CHUNK_MB --threads $((THREADS/4)) --tmp-dir $TMP_DIR --multipass-local-merge --merge-fan $MERGE_FAN"

echo -e "\n=== Benchmark completato! ==="
rm -f $INPUT_FILE
