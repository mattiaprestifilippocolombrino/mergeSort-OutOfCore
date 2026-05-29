#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build_bench}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_DIR/benchmark_results}"
DATA_DIR="${DATA_DIR:-$PROJECT_DIR/benchmark_data}"
TMP_BASE="${TMP_BASE:-${TMPDIR:-/tmp}}"

PAYLOAD_MAX_BUILD="${PAYLOAD_MAX_BUILD:-1048576}"
CHUNK_MB="${CHUNK_MB:-64}"
MERGE_FAN="${MERGE_FAN:-8}"
TRIALS="${TRIALS:-1}"
VERIFY="${VERIFY:-0}"
SEED="${SEED:-42}"
RUN_TIMEOUT_SECONDS="${RUN_TIMEOUT_SECONDS:-0}"

# Two intentionally different regimes:
# - many short records: stresses comparisons, indexing, task scheduling;
# - fewer large records: stresses I/O bandwidth and payload movement.
BENCHMARK_CASES="${BENCHMARK_CASES:-manySmall50M:50000000:64}"

THREAD_LIST="${THREAD_LIST:-1 2 4 8 16 32}"
MPI_THREAD_LIST="${MPI_THREAD_LIST:-1 4 16}"
STRONG_NODES="${STRONG_NODES:-1 2 4 8}"
RANKS_PER_NODE="${RANKS_PER_NODE:-1}"
WEAK_RECORDS_PER_NODE="${WEAK_RECORDS_PER_NODE:-6250000}"
WEAK_PAYLOAD_MAX="${WEAK_PAYLOAD_MAX:-64}"
WEAK_CASES="${WEAK_CASES:-weak_p${WEAK_PAYLOAD_MAX}_rpn${WEAK_RECORDS_PER_NODE}:${WEAK_RECORDS_PER_NODE}:${WEAK_PAYLOAD_MAX}}"

mkdir -p "$RESULTS_DIR" "$DATA_DIR" "$TMP_BASE"

log() {
    printf '[bench] %s\n' "$*" >&2
}

require_uint() {
    local name="$1"
    local value="$2"
    if ! [[ "$value" =~ ^[0-9]+$ ]]; then
        log "$name deve essere un intero non negativo, valore ricevuto: $value"
        return 2
    fi
}

require_positive_uint() {
    local name="$1"
    local value="$2"
    require_uint "$name" "$value"
    if (( value <= 0 )); then
        log "$name deve essere > 0, valore ricevuto: $value"
        return 2
    fi
}

validate_benchmark_config() {
    require_positive_uint "PAYLOAD_MAX_BUILD" "$PAYLOAD_MAX_BUILD"
    require_positive_uint "CHUNK_MB" "$CHUNK_MB"
    require_positive_uint "MERGE_FAN" "$MERGE_FAN"
    require_positive_uint "TRIALS" "$TRIALS"
    require_uint "RUN_TIMEOUT_SECONDS" "$RUN_TIMEOUT_SECONDS"

    local chunk_bytes=$((CHUNK_MB * 1024 * 1024))
    local min_record_bytes=$((PAYLOAD_MAX_BUILD + 12))
    if (( chunk_bytes < min_record_bytes )); then
        log "CHUNK_MB=$CHUNK_MB troppo piccolo per PAYLOAD_MAX_BUILD=$PAYLOAD_MAX_BUILD"
        log "Serve almeno $min_record_bytes byte per contenere un record massimo."
        return 2
    fi

    local threads
    for threads in $THREAD_LIST; do
        require_positive_uint "THREAD_LIST entry" "$threads"
    done

    local mpi_threads
    for mpi_threads in $MPI_THREAD_LIST; do
        require_positive_uint "MPI_THREAD_LIST entry" "$mpi_threads"
    done
}

build_project() {
    log "Configuro build Release in $BUILD_DIR con PAYLOAD_MAX=$PAYLOAD_MAX_BUILD"
    local cmake_args=(
        -S "$PROJECT_DIR"
        -B "$BUILD_DIR"
        -DCMAKE_BUILD_TYPE=Release
        -DPAYLOAD_MAX="$PAYLOAD_MAX_BUILD"
    )
    if [[ -n "${FF_ROOT:-}" ]]; then
        cmake_args+=("-DFF_ROOT=$FF_ROOT")
        log "FF_ROOT=$FF_ROOT"
    fi
    cmake "${cmake_args[@]}"
    cmake --build "$BUILD_DIR" -j "$(available_cpus)"
}

available_cpus() {
    if [[ -n "${SLURM_CPUS_PER_TASK:-}" ]]; then
        printf '%s\n' "$SLURM_CPUS_PER_TASK"
    elif command -v nproc >/dev/null 2>&1; then
        nproc
    else
        printf '1\n'
    fi
}

run_single() {
    local cpus="$1"
    shift

    if [[ -n "${SLURM_JOB_ID:-}" ]]; then
        srun -N 1 -n 1 -c "$cpus" "$@"
    else
        "$@"
    fi
}

run_single_benchmark() {
    local cpus="$1"
    shift

    if (( RUN_TIMEOUT_SECONDS > 0 )); then
        if [[ -n "${SLURM_JOB_ID:-}" ]]; then
            timeout --kill-after=10 "$RUN_TIMEOUT_SECONDS" \
                srun -N 1 -n 1 -c "$cpus" "$@"
        else
            timeout --kill-after=10 "$RUN_TIMEOUT_SECONDS" "$@"
        fi
    else
        run_single "$cpus" "$@"
    fi
}

run_mpi() {
    local nodes="$1"
    local ranks="$2"
    local threads="$3"
    shift 3

    if [[ -n "${SLURM_JOB_ID:-}" ]]; then
        srun --mpi=pmix --cpu-bind=none -N "$nodes" -n "$ranks" --ntasks-per-node "${RANKS_PER_NODE:-1}" -c "$threads" "$@"
    elif command -v mpirun >/dev/null 2>&1; then
        mpirun --oversubscribe -n "$ranks" "$@"
    else
        log "mpirun non trovato: impossibile eseguire benchmark MPI fuori da SLURM"
        return 127
    fi
}

case_name() {
    cut -d: -f1 <<<"$1"
}

case_records() {
    cut -d: -f2 <<<"$1"
}

case_payload() {
    cut -d: -f3 <<<"$1"
}

dataset_path() {
    local name="$1"
    local records="$2"
    local payload="$3"
    printf '%s/%s_n%s_p%s.bin\n' "$DATA_DIR" "$name" "$records" "$payload"
}

ensure_dataset() {
    local name="$1"
    local records="$2"
    local payload="$3"
    local path
    path="$(dataset_path "$name" "$records" "$payload")"

    if [[ -s "$path" ]]; then
        log "Dataset gia' presente: $path"
        printf '%s\n' "$path"
        return 0
    fi

    if (( payload > PAYLOAD_MAX_BUILD )); then
        log "payload=$payload supera PAYLOAD_MAX_BUILD=$PAYLOAD_MAX_BUILD"
        return 2
    fi

    log "Genero dataset $name: N=$records payload_max=$payload"
    run_single "$(available_cpus)" "$BUILD_DIR/generate" "$path" "$records" \
        --payload-max "$payload" \
        --seed "$SEED" >/dev/null
    printf '%s\n' "$path"
}

extract_seconds() {
    local label="$1"
    awk -v label="$label" '
        index($0, label) {
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$/) {
                    value = $i
                }
            }
        }
        END {
            if (value == "") {
                print "nan"
            } else {
                print value
            }
        }
    '
}

extract_generated_runs() {
    awk '
        index($0, "Fase 1") {
            for (i = 2; i <= NF; ++i) {
                if ($i == "run" && $(i - 1) ~ /^[0-9]+$/) {
                    value = $(i - 1)
                }
            }
        }
        END {
            if (value == "") {
                print "0"
            } else {
                print value
            }
        }
    '
}

run_and_capture_sort() {
    local out_log="$1"
    shift
    "$@" >"$out_log" 2>&1
}

write_csv_header() {
    local file="$1"
    local header="$2"
    if [[ "${APPEND_RESULTS:-0}" == "1" && -s "$file" ]]; then
        return
    else
        printf '%s\n' "$header" >"$file"
    fi
}

verify_output() {
    local input="$1"
    local output="$2"
    if [[ "$VERIFY" == "1" ]]; then
        "$BUILD_DIR/verify" "$input" "$output" >/dev/null
    fi
}
