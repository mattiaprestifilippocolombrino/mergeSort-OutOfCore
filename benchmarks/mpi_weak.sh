#!/usr/bin/env bash

set -Eeuo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/common.sh"

CSV="${1:-$RESULTS_DIR/mpi_weak_raw.csv}"

validate_benchmark_config

prepare_build

if [[ ! -x "$BUILD_DIR/mpi_sort" ]]; then
    log "mpi_sort non presente: MPI non trovato in fase di build."
    exit 1
fi

write_csv_header "$CSV" \
    "suite,case,trial,records,payload_max,nodes,ranks,ranks_per_node,threads_per_rank,total_cores,records_per_node,chunk_mb,merge_fan,time_budget_s,probe_chunks_per_rank,local_merge_impl,generated_runs,input_bytes,total_gib,gib_per_node,capacity_total_gib,capacity_gib_per_node,throughput_gib_s,throughput_gib_node_s,sort_s,merge_s,total_s,verified,log_file"

mpi_local_merge_args=(--multipass-local-merge)

allocated_nodes="${SLURM_JOB_NUM_NODES:-0}"
node_count="$(wc -w <<<"$STRONG_NODES")"
thread_count="$(wc -w <<<"$MPI_THREAD_LIST")"
planned_runs=$((node_count * thread_count * TRIALS))
log "Run MPI weak capacity pianificate al massimo: $planned_runs ($node_count nodi x $thread_count thread/rank x $TRIALS trial)"
log "Weak capacity: budget=${WEAK_TIME_BUDGET_SECONDS}s chunk_mb=$CHUNK_MB merge_fan=$MERGE_FAN probe_chunks/rank=$WEAK_PROBE_CHUNKS_PER_RANK"

estimate_records_per_rank() {
    local payload="$1"
    local avg_record_bytes=$((12 + (8 + payload) / 2))
    local probe_bytes=$((CHUNK_MB * 1024 * 1024 * WEAK_PROBE_CHUNKS_PER_RANK))
    local records=$((probe_bytes / avg_record_bytes))
    if (( records < 1 )); then
        records=1
    fi
    printf '%s\n' "$records"
}

format_capacity_metrics() {
    local input_bytes="$1"
    local nodes="$2"
    local total_s="$3"
    python3 - "$input_bytes" "$nodes" "$total_s" "$WEAK_TIME_BUDGET_SECONDS" <<'PY'
import math
import sys

input_bytes = int(sys.argv[1])
nodes = int(sys.argv[2])
total_s = float(sys.argv[3])
budget_s = float(sys.argv[4])
gib = input_bytes / (1024 ** 3)
gib_per_node = gib / nodes if nodes > 0 else math.nan
if math.isfinite(total_s) and total_s > 0:
    throughput = gib / total_s
    capacity_total = throughput * budget_s
    capacity_node = capacity_total / nodes if nodes > 0 else math.nan
else:
    throughput = math.nan
    capacity_total = math.nan
    capacity_node = math.nan
throughput_node = throughput / nodes if nodes > 0 else math.nan
print(",".join(f"{value:.9g}" for value in (gib, gib_per_node, capacity_total, capacity_node, throughput, throughput_node)))
PY
}

payload="$WEAK_PAYLOAD_MAX"
records_per_rank="$(estimate_records_per_rank "$payload")"
case_name_weak="weak_capacity_p${payload}_c${CHUNK_MB}_f${MERGE_FAN}_probe${WEAK_PROBE_CHUNKS_PER_RANK}"

for nodes in $STRONG_NODES; do
    if [[ -n "${SLURM_JOB_ID:-}" && "$allocated_nodes" -gt 0 && "$nodes" -gt "$allocated_nodes" ]]; then
        log "Salto nodes=$nodes: allocazione SLURM disponibile=$allocated_nodes"
        continue
    fi

    ranks=$((nodes * RANKS_PER_NODE))
    records=$((records_per_rank * ranks))
    records_per_node=$((records_per_rank * RANKS_PER_NODE))
    input="$(ensure_dataset "$case_name_weak" "$records" "$payload")"
    input_bytes="$(stat -c '%s' "$input")"
    mpi_input="$(stage_mpi_input "$input" "$nodes")"

    for threads in $MPI_THREAD_LIST; do
        total_cores=$((ranks * threads))
        if (( threads > 1 )); then
            mpi_local_merge_impl="mpi_local_omp_multipass"
        else
            mpi_local_merge_impl="mpi_local_multipass"
        fi

        for trial in $(seq 1 "$TRIALS"); do
            output="$TMP_BASE/out_weak_${case_name_weak}_n${nodes}_r${ranks}_t${threads}_i${trial}.bin"
            log_file="$LOG_DIR/mpi_weak_${case_name_weak}_n${nodes}_r${ranks}_t${threads}_i${trial}.log"

            log "MPI weak capacity nodes=$nodes records=$records bytes=$input_bytes ranks=$ranks threads/rank=$threads trial=$trial"
            if ! run_and_capture_sort "$log_file" \
                run_mpi "$nodes" "$ranks" "$threads" "$BUILD_DIR/mpi_sort" "$mpi_input" "$output" \
                    --chunk-mb "$CHUNK_MB" \
                    --threads "$threads" \
                    --tmp-dir "$TMP_BASE" \
                    --merge-fan "$MERGE_FAN" \
                    "${mpi_local_merge_args[@]}"; then
                log "MPI weak capacity fallito: nodes=$nodes ranks=$ranks threads/rank=$threads trial=$trial. Vedi $log_file"
                rm -f "$output"
                continue
            fi

            if ! verify_output "$input" "$output"; then
                log "Verifica MPI weak capacity fallita: nodes=$nodes ranks=$ranks threads/rank=$threads trial=$trial"
                rm -f "$output"
                continue
            fi
            rm -f "$output"

            generated_runs="$(extract_generated_runs <"$log_file")"
            if [[ "$generated_runs" == "0" ]]; then
                generated_runs="NA"
            fi
            sort_s="$(extract_seconds "Fase 1" <"$log_file")"
            merge_s="$(extract_seconds "Fase 2" <"$log_file")"
            total_s="$(extract_seconds "Totale" <"$log_file")"
            IFS=, read -r total_gib gib_per_node capacity_total_gib capacity_gib_per_node throughput_gib_s throughput_gib_node_s \
                <<<"$(format_capacity_metrics "$input_bytes" "$nodes" "$total_s")"

            printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
                "mpi_weak_capacity" "$case_name_weak" "$trial" "$records" "$payload" \
                "$nodes" "$ranks" "$RANKS_PER_NODE" "$threads" "$total_cores" \
                "$records_per_node" "$CHUNK_MB" "$MERGE_FAN" "$WEAK_TIME_BUDGET_SECONDS" "$WEAK_PROBE_CHUNKS_PER_RANK" \
                "$mpi_local_merge_impl" "$generated_runs" "$input_bytes" "$total_gib" "$gib_per_node" \
                "$capacity_total_gib" "$capacity_gib_per_node" "$throughput_gib_s" "$throughput_gib_node_s" \
                "$sort_s" "$merge_s" "$total_s" "$VERIFY" "$log_file" >>"$CSV"
        done
    done
done

log "CSV scritto in $CSV"
