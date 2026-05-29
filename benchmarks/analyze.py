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
    baselines: dict[tuple[str, str, str, str], float] = {}
    for row in rows:
        key = (
            row["impl"],
            row["case"],
            row.get("chunk_mb", ""),
            row.get("merge_fan", ""),
        )
        threads = as_int(row, "threads")
        time = as_float(row, "avg_total_s")
        if threads == 1 and math.isfinite(time):
            baselines[key] = time

    for row in rows:
        key = (
            row["impl"],
            row["case"],
            row.get("chunk_mb", ""),
            row.get("merge_fan", ""),
        )
        baseline = baselines.get(key)
        threads = as_int(row, "threads")
        time = as_float(row, "avg_total_s")
        if baseline and time > 0 and threads > 0:
            speedup = baseline / time
            efficiency = speedup / threads
            row["baseline_total_s"] = f"{baseline:.9g}"
            row["speedup"] = f"{speedup:.9g}"
            row["efficiency"] = f"{efficiency:.9g}"
        else:
            row["baseline_total_s"] = "nan"
            row["speedup"] = "nan"
            row["efficiency"] = "nan"


def add_strong_metrics(rows: list[dict[str, str]]) -> None:
    baselines: dict[tuple[str, str, str], tuple[float, int]] = {}
    for row in rows:
        key = (row["case"], row["ranks_per_node"], row["threads_per_rank"])
        nodes = as_int(row, "nodes")
        time = as_float(row, "avg_total_s")
        if not math.isfinite(time) or nodes <= 0:
            continue
        old = baselines.get(key)
        if old is None or nodes < old[1]:
            baselines[key] = (time, nodes)

    for row in rows:
        baseline = baselines.get((row["case"], row["ranks_per_node"], row["threads_per_rank"]))
        nodes = as_int(row, "nodes")
        time = as_float(row, "avg_total_s")
        if baseline and time > 0 and nodes > 0:
            base_time, base_nodes = baseline
            speedup = base_time / time
            efficiency = speedup / (nodes / base_nodes)
            row["strong_speedup"] = f"{speedup:.9g}"
            row["strong_efficiency"] = f"{efficiency:.9g}"
            row["baseline_nodes"] = str(base_nodes)
        else:
            row["strong_speedup"] = "nan"
            row["strong_efficiency"] = "nan"
            row["baseline_nodes"] = "0"


def add_weak_metrics(rows: list[dict[str, str]]) -> None:
    baselines: dict[tuple[str, str, str], tuple[float, int]] = {}
    for row in rows:
        key = (row["case"], row["ranks_per_node"], row["threads_per_rank"])
        nodes = as_int(row, "nodes")
        time = as_float(row, "avg_total_s")
        if not math.isfinite(time) or nodes <= 0:
            continue
        old = baselines.get(key)
        if old is None or nodes < old[1]:
            baselines[key] = (time, nodes)

    for row in rows:
        baseline = baselines.get((row["case"], row["ranks_per_node"], row["threads_per_rank"]))
        time = as_float(row, "avg_total_s")
        if baseline and time > 0:
            base_time, base_nodes = baseline
            row["weak_efficiency"] = f"{base_time / time:.9g}"
            row["baseline_nodes"] = str(base_nodes)
        else:
            row["weak_efficiency"] = "nan"
            row["baseline_nodes"] = "0"


def maybe_plot(output_dir: Path, single: list[dict[str, str]], strong: list[dict[str, str]], weak: list[dict[str, str]]) -> None:
    try:
        import matplotlib.pyplot as plt
    except Exception:
        return

    plot_dir = output_dir / "plots"
    plot_dir.mkdir(parents=True, exist_ok=True)

    def safe(name: str) -> str:
        return "".join(c if c.isalnum() or c in "._-" else "_" for c in name)

    for (impl, case), group in grouped(single, ["impl", "case"]).items():
        group = sorted(group, key=lambda r: as_int(r, "threads"))
        xs = [as_int(r, "threads") for r in group]
        speedup = [as_float(r, "speedup") for r in group]
        efficiency = [as_float(r, "efficiency") for r in group]
        if not xs:
            continue
        fig, axes = plt.subplots(1, 2, figsize=(10, 4))
        axes[0].plot(xs, speedup, marker="o")
        axes[0].set_title(f"{impl} {case} speedup")
        axes[0].set_xlabel("threads/workers")
        axes[0].set_ylabel("speedup")
        axes[0].grid(True, alpha=0.3)
        axes[1].plot(xs, efficiency, marker="o")
        axes[1].set_title(f"{impl} {case} efficiency")
        axes[1].set_xlabel("threads/workers")
        axes[1].set_ylabel("efficiency")
        axes[1].grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(plot_dir / f"single_{safe(impl)}_{safe(case)}.png")
        plt.close(fig)

    for (case, threads), group in grouped(strong, ["case", "threads_per_rank"]).items():
        group = sorted(group, key=lambda r: as_int(r, "nodes"))
        xs = [as_int(r, "nodes") for r in group]
        speedup = [as_float(r, "strong_speedup") for r in group]
        efficiency = [as_float(r, "strong_efficiency") for r in group]
        if not xs:
            continue
        fig, axes = plt.subplots(1, 2, figsize=(10, 4))
        axes[0].plot(xs, speedup, marker="o")
        axes[0].set_title(f"strong speedup {case}, t/rank={threads}")
        axes[0].set_xlabel("nodes")
        axes[0].set_ylabel("speedup")
        axes[0].grid(True, alpha=0.3)
        axes[1].plot(xs, efficiency, marker="o")
        axes[1].set_title(f"strong efficiency {case}, t/rank={threads}")
        axes[1].set_xlabel("nodes")
        axes[1].set_ylabel("efficiency")
        axes[1].grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(plot_dir / f"strong_{safe(case)}_t{safe(threads)}.png")
        plt.close(fig)

    for (case, threads), group in grouped(weak, ["case", "threads_per_rank"]).items():
        group = sorted(group, key=lambda r: as_int(r, "nodes"))
        xs = [as_int(r, "nodes") for r in group]
        time = [as_float(r, "avg_total_s") for r in group]
        efficiency = [as_float(r, "weak_efficiency") for r in group]
        if not xs:
            continue
        fig, axes = plt.subplots(1, 2, figsize=(10, 4))
        axes[0].plot(xs, time, marker="o")
        axes[0].set_title(f"weak time {case}, t/rank={threads}")
        axes[0].set_xlabel("nodes")
        axes[0].set_ylabel("seconds")
        axes[0].grid(True, alpha=0.3)
        axes[1].plot(xs, efficiency, marker="o")
        axes[1].set_title(f"weak efficiency {case}, t/rank={threads}")
        axes[1].set_xlabel("nodes")
        axes[1].set_ylabel("efficiency")
        axes[1].grid(True, alpha=0.3)
        fig.tight_layout()
        fig.savefig(plot_dir / f"weak_{safe(case)}_t{safe(threads)}.png")
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
        ["impl", "case", "records", "payload_max", "threads", "chunk_mb", "merge_fan", "generated_runs"],
        drop_worst,
    )
    add_single_node_metrics(single)
    write_csv(results_dir / "single_node_summary.csv", single)

    tuning = summarize(
        read_csv(results_dir / "single_node_tuning_raw.csv"),
        ["impl", "case", "records", "payload_max", "threads", "chunk_mb", "merge_fan", "generated_runs"],
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
