#!/bin/bash
# ==============================================================================
# run_tuning.sh — Grid Search per tuning ottimale del MergeSort Out-Of-Core
# ==============================================================================
# Questo script esegue una ricerca dei parametri migliori (chunk-mb e merge-fan)
# per minimizzare il tempo di esecuzione e ottimizzare l'I/O.
# 
# Si consiglia di eseguire questo script su un file di test rappresentativo
# (es. 10GB-20GB) che sia comunque più grande della RAM allocata ai chunk.
#
# Esempio di utilizzo:
#   ./run_tuning.sh data/input_10G.bin data/output.bin
# ==============================================================================

if [ "$#" -ne 2 ]; then
    echo "Utilizzo: $0 <file_input> <file_output>"
    exit 1
fi

INPUT=$1
OUTPUT=$2

# Compilazione se necessaria
echo "[Tuning] Verifica binario OMP..."
if [ ! -f "build/omp/omp_sort" ]; then
    mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4 && cd ..
fi

BIN="./build/omp/omp_sort"

if [ ! -x "$BIN" ]; then
    echo "[Errore] Binario $BIN non trovato o non eseguibile."
    exit 1
fi

# Spazio di esplorazione dei parametri
CHUNK_SIZES=(64 128 256)
MERGE_FANS=(16 32 64)

# File di report temporaneo
REPORT="tuning_report.txt"
echo "--- Report Tuning ---" > $REPORT
echo "Input: $INPUT" >> $REPORT
printf "%-10s %-10s %-15s %-15s %-15s\n" "Chunk(MB)" "Fan-in" "Fase 1 (s)" "Fase 2 (s)" "Totale (s)" >> $REPORT
echo "----------------------------------------------------------------" >> $REPORT

echo "[Tuning] Avvio esplorazione parametri..."
echo "----------------------------------------------------------------"
printf "%-10s %-10s %-15s %-15s %-15s\n" "Chunk(MB)" "Fan-in" "Fase 1 (s)" "Fase 2 (s)" "Totale (s)"
echo "----------------------------------------------------------------"

BEST_TIME=9999999
BEST_CONFIG=""

for CHUNK in "${CHUNK_SIZES[@]}"; do
    for FAN in "${MERGE_FANS[@]}"; do
        
        # Elimina file precedenti per evitare cache hit spurie a livello OS, se possibile
        rm -f "$OUTPUT"
        
        # Esegui con la nuova pipeline multipass (--pipeline-merge)
        # Catturiamo lo stderr/stdout per estrarre i tempi
        LOG=$( $BIN "$INPUT" "$OUTPUT" --chunk-mb $CHUNK --merge-fan $FAN --pipeline-merge 2>&1 )
        
        # Estrazione tempi (basato sull'output standard di omp_sort)
        T_FASE1=$(echo "$LOG" | grep "Sort parallelo (Fase 1)" | grep -oE '[0-9]+(\.[0-9]+)?')
        T_FASE2=$(echo "$LOG" | grep "K-way merge   (Fase 2)" | grep -oE '[0-9]+(\.[0-9]+)?')
        T_TOT=$(echo "$LOG" | grep "Totale" | grep -oE '[0-9]+(\.[0-9]+)?' | tail -n 1)

        # Gestione fallimenti
        if [ -z "$T_TOT" ]; then
            T_TOT="ERR"
            T_FASE1="ERR"
            T_FASE2="ERR"
        fi

        printf "%-10s %-10s %-15s %-15s %-15s\n" "$CHUNK" "$FAN" "$T_FASE1" "$T_FASE2" "$T_TOT"
        printf "%-10s %-10s %-15s %-15s %-15s\n" "$CHUNK" "$FAN" "$T_FASE1" "$T_FASE2" "$T_TOT" >> $REPORT

        # Aggiornamento miglior tempo
        if [ "$T_TOT" != "ERR" ]; then
            if (( $(echo "$T_TOT < $BEST_TIME" | bc -l) )); then
                BEST_TIME=$T_TOT
                BEST_CONFIG="Chunk: ${CHUNK}MB, Fan-in: ${FAN}"
            fi
        fi

    done
done

echo "----------------------------------------------------------------"
echo "[Tuning] Completato!"
echo "Miglior configurazione trovata: $BEST_CONFIG (Tempo: ${BEST_TIME}s)"
echo "I risultati sono stati salvati in $REPORT"
