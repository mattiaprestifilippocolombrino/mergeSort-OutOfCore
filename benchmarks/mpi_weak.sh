#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CSV="${1:-$RESULTS_DIR/mpi_weak_raw.csv}"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    build_project
fi

if [[ ! -x "$BUILD_DIR/mpi_sort" ]]; then
    log "mpi_sort non presente: MPI non trovato in fase di build."
    exit 1
fi

write_csv_header "$CSV" \
    "suite,case,trial,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,generated_runs,sort_s,merge_s,total_s,verified,log_file"

allocated_nodes="${SLURM_JOB_NUM_NODES:-0}"

for weak_spec in $WEAK_CASES; do
    case_name_weak="$(case_name "$weak_spec")"
    records_per_node="$(case_records "$weak_spec")"
    payload="$(case_payload "$weak_spec")"

    for nodes in $STRONG_NODES; do
        if [[ -n "${SLURM_JOB_ID:-}" && "$allocated_nodes" -gt 0 && "$nodes" -gt "$allocated_nodes" ]]; then
            log "Salto nodes=$nodes: allocazione SLURM disponibile=$allocated_nodes"
            continue
        fi

        records=$((records_per_node * nodes))
        input="$(ensure_dataset "$case_name_weak" "$records" "$payload")"
        ranks=$((nodes * RANKS_PER_NODE))

        for threads in $MPI_THREAD_LIST; do
            total_cores=$((ranks * threads))

            for trial in $(seq 1 "$TRIALS"); do
                output="$DATA_DIR/out_weak_${case_name_weak}_n${nodes}_r${ranks}_t${threads}_i${trial}.bin"
                log_file="$RESULTS_DIR/mpi_weak_${case_name_weak}_n${nodes}_r${ranks}_t${threads}_i${trial}.log"

                log "MPI weak case=$case_name_weak nodes=$nodes records=$records ranks=$ranks threads/rank=$threads trial=$trial"
                run_and_capture_sort "$log_file" \
                    run_mpi "$nodes" "$ranks" "$threads" "$BUILD_DIR/mpi_sort" "$input" "$output" \
                        --chunk-mb "$CHUNK_MB" \
                        --threads "$threads" \
                        --tmp-dir "$TMP_BASE" \
                        --merge-fan "$MERGE_FAN"

                verify_output "$input" "$output"
                rm -f "$output"

                generated_runs="$(extract_generated_runs <"$log_file")"
                sort_s="$(extract_seconds "Fase 1" <"$log_file")"
                merge_s="$(extract_seconds "Fase 2" <"$log_file")"
                total_s="$(extract_seconds "Totale" <"$log_file")"

                printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
                    "mpi_weak" "$case_name_weak" "$trial" "$records" "$payload" \
                    "$nodes" "$ranks" "$RANKS_PER_NODE" "$threads" "$total_cores" \
                    "$records_per_node" "$CHUNK_MB" "$MERGE_FAN" "$generated_runs" \
                    "$sort_s" "$merge_s" "$total_s" "$VERIFY" "$log_file" >>"$CSV"
            done
        done
    done
done

log "CSV scritto in $CSV"
