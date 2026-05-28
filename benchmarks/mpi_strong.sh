#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CSV="${1:-$RESULTS_DIR/mpi_strong_raw.csv}"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    build_project
fi

if [[ ! -x "$BUILD_DIR/mpi_sort" ]]; then
    log "mpi_sort non presente: MPI non trovato in fase di build."
    exit 1
fi

write_csv_header "$CSV" \
    "suite,case,trial,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,chunk_mb,merge_fan,sort_s,merge_s,total_s,verified,log_file"

allocated_nodes="${SLURM_JOB_NUM_NODES:-0}"

for spec in $BENCHMARK_CASES; do
    name="$(case_name "$spec")"
    records="$(case_records "$spec")"
    payload="$(case_payload "$spec")"
    input="$(ensure_dataset "$name" "$records" "$payload")"

    for nodes in $STRONG_NODES; do
        if [[ -n "${SLURM_JOB_ID:-}" && "$allocated_nodes" -gt 0 && "$nodes" -gt "$allocated_nodes" ]]; then
            log "Salto nodes=$nodes: allocazione SLURM disponibile=$allocated_nodes"
            continue
        fi

        ranks=$((nodes * RANKS_PER_NODE))

        for threads in $MPI_THREAD_LIST; do
            total_cores=$((ranks * threads))

            for trial in $(seq 1 "$TRIALS"); do
                output="$DATA_DIR/out_strong_${name}_n${nodes}_r${ranks}_t${threads}_i${trial}.bin"
                log_file="$RESULTS_DIR/mpi_strong_${name}_n${nodes}_r${ranks}_t${threads}_i${trial}.log"

                log "MPI strong case=$name nodes=$nodes ranks=$ranks threads/rank=$threads trial=$trial"
                run_and_capture_sort "$log_file" \
                    run_mpi "$nodes" "$ranks" "$threads" "$BUILD_DIR/mpi_sort" "$input" "$output" \
                        --chunk-mb "$CHUNK_MB" \
                        --threads "$threads" \
                        --tmp-dir "$TMP_BASE" \
                        --merge-fan "$MERGE_FAN"

                verify_output "$input" "$output"
                rm -f "$output"

                sort_s="$(extract_seconds "Fase 1" <"$log_file")"
                merge_s="$(extract_seconds "Fase 2" <"$log_file")"
                total_s="$(extract_seconds "Totale" <"$log_file")"

                printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
                    "mpi_strong" "$name" "$trial" "$records" "$payload" \
                    "$nodes" "$ranks" "$RANKS_PER_NODE" "$threads" "$total_cores" \
                    "$CHUNK_MB" "$MERGE_FAN" "$sort_s" "$merge_s" "$total_s" \
                    "$VERIFY" "$log_file" >>"$CSV"
            done
        done
    done
done

log "CSV scritto in $CSV"
