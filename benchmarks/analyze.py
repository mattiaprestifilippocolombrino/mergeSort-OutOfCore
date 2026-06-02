#!/usr/bin/env python3
"""Aggregate benchmark CSVs and compute speedup/efficiency metrics.

The script deliberately uses the Python standard library for the numeric
summaries. If matplotlib is installed, it also writes a small set of PNG plots.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def as_float(row: dict[str, str], key: str) -> float:
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return math.nan


def as_int(row: dict[str, str], key: str) -> int:
    try:
        return int(row[key])
    except (KeyError, TypeError, ValueError):
        return 0


def grouped(rows: list[dict[str, str]], keys: list[str]):
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[tuple(row.get(k, "") for k in keys)].append(row)
    return groups


def summarize(rows: list[dict[str, str]], keys: list[str], drop_worst: bool) -> list[dict[str, str]]:
    out: list[dict[str, str]] = []
    for values, group in grouped(rows, keys).items():
        times = [as_float(row, "total_s") for row in group]
        times = [t for t in times if math.isfinite(t)]
        if drop_worst and len(times) >= 3:
            times.remove(max(times))
        if not times:
            continue

        template = {k: v for k, v in zip(keys, values)}
        template["trials"] = str(len(times))
        template["avg_total_s"] = f"{statistics.fmean(times):.9g}"
        template["min_total_s"] = f"{min(times):.9g}"
        template["max_total_s"] = f"{max(times):.9g}"
        template["stdev_total_s"] = f"{statistics.stdev(times):.9g}" if len(times) > 1 else "0"

        for phase in ("sort_s", "merge_s"):
            phase_times = [as_float(row, phase) for row in group]
            phase_times = [t for t in phase_times if math.isfinite(t)]
            if drop_worst and len(phase_times) >= 3:
                phase_times.remove(max(phase_times))
            template[f"avg_{phase}"] = f"{statistics.fmean(phase_times):.9g}" if phase_times else "nan"

        out.append(template)
    return out


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def add_single_node_metrics(rows: list[dict[str, str]]) -> None:
    baselines: dict[tuple[str, str, str, str], dict[str, float]] = {}
    for row in rows:
        key = (
            row["impl"],
            row.get("merge_impl", ""),
            row["case"],
            row.get("chunk_mb", ""),
            row.get("merge_fan", ""),
        )
        threads = as_int(row, "threads")
        if threads == 1:
            baselines[key] = {
                "total": as_float(row, "avg_total_s"),
                "sort": as_float(row, "avg_sort_s"),
                "merge": as_float(row, "avg_merge_s"),
            }

    for row in rows:
        key = (
            row["impl"],
            row.get("merge_impl", ""),
            row["case"],
            row.get("chunk_mb", ""),
            row.get("merge_fan", ""),
        )
        threads = as_int(row, "threads")
        baseline = baselines.get(key, {})

        def add_phase_metrics(label: str, avg_key: str, speedup_key: str, efficiency_key: str) -> None:
            base_time = baseline.get(label, math.nan)
            time = as_float(row, avg_key)
            row[f"baseline_{label}_s"] = f"{base_time:.9g}" if math.isfinite(base_time) else "nan"
            if math.isfinite(base_time) and base_time > 0 and time > 0 and threads > 0:
                speedup = base_time / time
                row[speedup_key] = f"{speedup:.9g}"
                row[efficiency_key] = f"{speedup / threads:.9g}"
            else:
                row[speedup_key] = "nan"
                row[efficiency_key] = "nan"

        add_phase_metrics("total", "avg_total_s", "speedup", "efficiency")
        add_phase_metrics("sort", "avg_sort_s", "sort_speedup", "sort_efficiency")
        add_phase_metrics("merge", "avg_merge_s", "merge_speedup", "merge_efficiency")
        row["total_speedup"] = row["speedup"]
        row["total_efficiency"] = row["efficiency"]
        row["phase1_speedup"] = row["sort_speedup"]
        row["phase1_efficiency"] = row["sort_efficiency"]
        row["phase2_speedup"] = row["merge_speedup"]
        row["phase2_efficiency"] = row["merge_efficiency"]


def add_strong_metrics(rows: list[dict[str, str]]) -> None:
    baselines: dict[tuple[str, str, str, str], tuple[dict[str, float], int]] = {}
    for row in rows:
        key = (
            row["case"],
            row.get("local_merge_impl", ""),
            row["ranks_per_node"],
            row["threads_per_rank"],
        )
        nodes = as_int(row, "nodes")
        total_time = as_float(row, "avg_total_s")
        if not math.isfinite(total_time) or nodes <= 0:
            continue
        old = baselines.get(key)
        if old is None or nodes < old[1]:
            baselines[key] = (
                {
                    "total": total_time,
                    "sort": as_float(row, "avg_sort_s"),
                    "merge": as_float(row, "avg_merge_s"),
                },
                nodes,
            )

    for row in rows:
        baseline = baselines.get(
            (
                row["case"],
                row.get("local_merge_impl", ""),
                row["ranks_per_node"],
                row["threads_per_rank"],
            )
        )
        nodes = as_int(row, "nodes")
        if baseline and nodes > 0:
            base_times, base_nodes = baseline
            row["baseline_nodes"] = str(base_nodes)
            scale = nodes / base_nodes
        else:
            base_times = {}
            scale = math.nan
            row["baseline_nodes"] = "0"

        def add_phase_metrics(label: str, avg_key: str, speedup_key: str, efficiency_key: str) -> None:
            base_time = base_times.get(label, math.nan)
            time = as_float(row, avg_key)
            if math.isfinite(base_time) and base_time > 0 and time > 0 and math.isfinite(scale) and scale > 0:
                speedup = base_time / time
                row[speedup_key] = f"{speedup:.9g}"
                row[efficiency_key] = f"{speedup / scale:.9g}"
            else:
                row[speedup_key] = "nan"
                row[efficiency_key] = "nan"

        add_phase_metrics("total", "avg_total_s", "strong_speedup", "strong_efficiency")
        add_phase_metrics("sort", "avg_sort_s", "strong_sort_speedup", "strong_sort_efficiency")
        add_phase_metrics("merge", "avg_merge_s", "strong_merge_speedup", "strong_merge_efficiency")
        row["total_speedup"] = row["strong_speedup"]
        row["total_efficiency"] = row["strong_efficiency"]
        row["phase1_speedup"] = row["strong_sort_speedup"]
        row["phase1_efficiency"] = row["strong_sort_efficiency"]
        row["phase2_speedup"] = row["strong_merge_speedup"]
        row["phase2_efficiency"] = row["strong_merge_efficiency"]


def add_weak_metrics(rows: list[dict[str, str]]) -> None:
    baselines: dict[tuple[str, str, str, str], tuple[dict[str, float], int]] = {}
    for row in rows:
        key = (
            row["case"],
            row.get("local_merge_impl", ""),
            row["ranks_per_node"],
            row["threads_per_rank"],
        )
        nodes = as_int(row, "nodes")
        time = as_float(row, "avg_total_s")
        if not math.isfinite(time) or nodes <= 0:
            continue
        old = baselines.get(key)
        if old is None or nodes < old[1]:
            baselines[key] = (
                {
                    "total": time,
                    "sort": as_float(row, "avg_sort_s"),
                    "merge": as_float(row, "avg_merge_s"),
                },
                nodes,
            )

    for row in rows:
        baseline = baselines.get(
            (
                row["case"],
                row.get("local_merge_impl", ""),
                row["ranks_per_node"],
                row["threads_per_rank"],
            )
        )
        if baseline:
            base_times, base_nodes = baseline
            row["baseline_nodes"] = str(base_nodes)
        else:
            base_times = {}
            row["baseline_nodes"] = "0"

        def add_phase_metrics(label: str, avg_key: str, speedup_key: str, efficiency_key: str) -> None:
            base_time = base_times.get(label, math.nan)
            time = as_float(row, avg_key)
            if math.isfinite(base_time) and base_time > 0 and time > 0:
                speedup = base_time / time
                row[speedup_key] = f"{speedup:.9g}"
                row[efficiency_key] = f"{speedup:.9g}"
            else:
                row[speedup_key] = "nan"
                row[efficiency_key] = "nan"

        add_phase_metrics("total", "avg_total_s", "weak_speedup", "weak_efficiency")
        add_phase_metrics("sort", "avg_sort_s", "weak_sort_speedup", "weak_sort_efficiency")
        add_phase_metrics("merge", "avg_merge_s", "weak_merge_speedup", "weak_merge_efficiency")
        row["total_speedup"] = row["weak_speedup"]
        row["total_efficiency"] = row["weak_efficiency"]
        row["phase1_speedup"] = row["weak_sort_speedup"]
        row["phase1_efficiency"] = row["weak_sort_efficiency"]
        row["phase2_speedup"] = row["weak_merge_speedup"]
        row["phase2_efficiency"] = row["weak_merge_efficiency"]


def maybe_plot(output_dir: Path, single: list[dict[str, str]], strong: list[dict[str, str]], weak: list[dict[str, str]]) -> None:
    try:
        import matplotlib.pyplot as plt
    except Exception:
        return

    plot_dir = output_dir / "plots"
    plot_dir.mkdir(parents=True, exist_ok=True)

    def safe(name: str) -> str:
        return "".join(c if c.isalnum() or c in "._-" else "_" for c in name)

    for (impl, merge_impl, case), group in grouped(single, ["impl", "merge_impl", "case"]).items():
        group = sorted(group, key=lambda r: as_int(r, "threads"))
        xs = [as_int(r, "threads") for r in group]
        if not xs:
            continue
        title = f"{impl} {merge_impl} {case}".replace("  ", " ")
        fig, axes = plt.subplots(1, 2, figsize=(10, 4))
        axes[0].plot(xs, [as_float(r, "speedup") for r in group], marker="o", label="total")
        axes[0].plot(xs, [as_float(r, "sort_speedup") for r in group], marker="o", label="sort")
        axes[0].plot(xs, [as_float(r, "merge_speedup") for r in group], marker="o", label="merge")
        axes[0].set_title(f"{title} speedup")
        axes[0].set_xlabel("threads/workers")
        axes[0].set_ylabel("speedup")
        axes[0].grid(True, alpha=0.3)
        axes[0].legend()
        axes[1].plot(xs, [as_float(r, "efficiency") for r in group], marker="o", label="total")
        axes[1].plot(xs, [as_float(r, "sort_efficiency") for r in group], marker="o", label="sort")
        axes[1].plot(xs, [as_float(r, "merge_efficiency") for r in group], marker="o", label="merge")
        axes[1].set_title(f"{title} efficiency")
        axes[1].set_xlabel("threads/workers")
        axes[1].set_ylabel("efficiency")
        axes[1].grid(True, alpha=0.3)
        axes[1].legend()
        fig.tight_layout()
        fig.savefig(plot_dir / f"single_{safe(impl)}_{safe(merge_impl)}_{safe(case)}.png")
        plt.close(fig)

    for (case, local_merge_impl, threads), group in grouped(strong, ["case", "local_merge_impl", "threads_per_rank"]).items():
        group = sorted(group, key=lambda r: as_int(r, "nodes"))
        xs = [as_int(r, "nodes") for r in group]
        if not xs:
            continue
        fig, axes = plt.subplots(1, 2, figsize=(10, 4))
        axes[0].plot(xs, [as_float(r, "strong_speedup") for r in group], marker="o", label="total")
        axes[0].plot(xs, [as_float(r, "strong_sort_speedup") for r in group], marker="o", label="phase 1")
        axes[0].plot(xs, [as_float(r, "strong_merge_speedup") for r in group], marker="o", label="phase 2")
        axes[0].set_title(f"strong speedup {case}, {local_merge_impl}, t/rank={threads}")
        axes[0].set_xlabel("nodes")
        axes[0].set_ylabel("speedup")
        axes[0].grid(True, alpha=0.3)
        axes[0].legend()
        axes[1].plot(xs, [as_float(r, "strong_efficiency") for r in group], marker="o", label="total")
        axes[1].plot(xs, [as_float(r, "strong_sort_efficiency") for r in group], marker="o", label="phase 1")
        axes[1].plot(xs, [as_float(r, "strong_merge_efficiency") for r in group], marker="o", label="phase 2")
        axes[1].set_title(f"strong efficiency {case}, {local_merge_impl}, t/rank={threads}")
        axes[1].set_xlabel("nodes")
        axes[1].set_ylabel("efficiency")
        axes[1].grid(True, alpha=0.3)
        axes[1].legend()
        fig.tight_layout()
        fig.savefig(plot_dir / f"strong_{safe(case)}_{safe(local_merge_impl)}_t{safe(threads)}.png")
        plt.close(fig)

    for (case, local_merge_impl, threads), group in grouped(weak, ["case", "local_merge_impl", "threads_per_rank"]).items():
        group = sorted(group, key=lambda r: as_int(r, "nodes"))
        xs = [as_int(r, "nodes") for r in group]
        if not xs:
            continue
        fig, axes = plt.subplots(1, 2, figsize=(10, 4))
        axes[0].plot(xs, [as_float(r, "avg_total_s") for r in group], marker="o", label="total")
        axes[0].plot(xs, [as_float(r, "avg_sort_s") for r in group], marker="o", label="phase 1")
        axes[0].plot(xs, [as_float(r, "avg_merge_s") for r in group], marker="o", label="phase 2")
        axes[0].set_title(f"weak time {case}, {local_merge_impl}, t/rank={threads}")
        axes[0].set_xlabel("nodes")
        axes[0].set_ylabel("seconds")
        axes[0].grid(True, alpha=0.3)
        axes[0].legend()
        axes[1].plot(xs, [as_float(r, "weak_efficiency") for r in group], marker="o", label="total")
        axes[1].plot(xs, [as_float(r, "weak_sort_efficiency") for r in group], marker="o", label="phase 1")
        axes[1].plot(xs, [as_float(r, "weak_merge_efficiency") for r in group], marker="o", label="phase 2")
        axes[1].set_title(f"weak efficiency {case}, {local_merge_impl}, t/rank={threads}")
        axes[1].set_xlabel("nodes")
        axes[1].set_ylabel("efficiency")
        axes[1].grid(True, alpha=0.3)
        axes[1].legend()
        fig.tight_layout()
        fig.savefig(plot_dir / f"weak_{safe(case)}_{safe(local_merge_impl)}_t{safe(threads)}.png")
        plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description="Aggregate SPM benchmark CSVs.")
    parser.add_argument("--results-dir", default="benchmark_results")
    parser.add_argument("--keep-worst", action="store_true", help="Do not drop the slowest trial when at least 3 trials exist.")
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    drop_worst = not args.keep_worst

    single = summarize(
        read_csv(results_dir / "single_node_raw.csv"),
        ["impl", "merge_impl", "case", "records", "payload_max", "threads", "chunk_mb", "merge_fan", "generated_runs"],
        drop_worst,
    )
    add_single_node_metrics(single)
    write_csv(results_dir / "single_node_summary.csv", single)

    tuning = summarize(
        read_csv(results_dir / "single_node_tuning_raw.csv"),
        ["impl", "merge_impl", "case", "records", "payload_max", "threads", "chunk_mb", "merge_fan", "generated_runs"],
        drop_worst,
    )
    add_single_node_metrics(tuning)
    write_csv(results_dir / "single_node_tuning_summary.csv", tuning)

    strong = summarize(
        read_csv(results_dir / "mpi_strong_raw.csv"),
        [
            "case",
            "records",
            "payload_max",
            "nodes",
            "ranks",
            "ranks_per_node",
            "threads_per_rank",
            "total_cores",
            "chunk_mb",
            "merge_fan",
            "local_merge_impl",
            "generated_runs",
        ],
        drop_worst,
    )
    add_strong_metrics(strong)
    write_csv(results_dir / "mpi_strong_summary.csv", strong)

    weak = summarize(
        read_csv(results_dir / "mpi_weak_raw.csv"),
        [
            "case",
            "records",
            "payload_max",
            "nodes",
            "ranks",
            "ranks_per_node",
            "threads_per_rank",
            "total_cores",
            "records_per_node",
            "chunk_mb",
            "merge_fan",
            "local_merge_impl",
            "generated_runs",
        ],
        drop_worst,
    )
    add_weak_metrics(weak)
    write_csv(results_dir / "mpi_weak_summary.csv", weak)
    maybe_plot(results_dir, single, strong, weak)

    print(f"single-node rows: {len(single)} -> {results_dir / 'single_node_summary.csv'}")
    print(f"tuning rows     : {len(tuning)} -> {results_dir / 'single_node_tuning_summary.csv'}")
    print(f"MPI strong rows : {len(strong)} -> {results_dir / 'mpi_strong_summary.csv'}")
    print(f"MPI weak rows   : {len(weak)} -> {results_dir / 'mpi_weak_summary.csv'}")


if __name__ == "__main__":
    main()
