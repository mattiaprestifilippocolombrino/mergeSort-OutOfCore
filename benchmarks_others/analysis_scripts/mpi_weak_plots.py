#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt

def plot_weak_scaling(csv_file, output_prefix="weak_scaling"):
    # Load CSV
    df = pd.read_csv(csv_file)

    # --- Execution Time Plot ---
    plt.figure(figsize=(7,5))
    for threads, sub in df.groupby("num_threads"):
        sub = sub.sort_values("num_nodes")
        plt.plot(sub["num_nodes"], sub["avg_time(ms)"], marker="o", label=f"{threads} threads")

    plt.xlabel("Number of Nodes")
    plt.ylabel("Execution Time (ms)")
    plt.title("Weak Scaling: Execution Time")
    plt.xticks(sorted(df["num_nodes"].unique()))
    plt.grid(True, ls="--", alpha=0.7)
    plt.legend(title="Threads")
    plt.tight_layout()
    plt.savefig(f"{output_prefix}_time.png")
    plt.close()

    # --- Efficiency Plot (baseline = 2 nodes) ---
    plt.figure(figsize=(7,5))
    for threads, sub in df.groupby("num_threads"):
        sub = sub.sort_values("num_nodes")
        baseline_time = sub[sub["num_nodes"] == 2]["avg_time(ms)"].iloc[0]
        efficiency = baseline_time / sub["avg_time(ms)"]
        plt.plot(sub["num_nodes"], efficiency, marker="s", label=f"{threads} threads")

    plt.axhline(1.0, color="gray", linestyle="--", label="Ideal Scaling (2-node baseline)")
    plt.xlabel("Number of Nodes")
    plt.ylabel("Efficiency (T2 / Tp)")
    plt.title("Weak Scaling: Efficiency (2-node baseline)")
    plt.xticks(sorted(df["num_nodes"].unique()))
    plt.grid(True, ls="--", alpha=0.7)
    plt.legend(title="Threads")
    plt.tight_layout()
    plt.savefig(f"{output_prefix}_eff.png")
    plt.close()

    print("✅ Plots saved as:")
    print(f"   {output_prefix}_time.png")
    print(f"   {output_prefix}_eff.png")


if __name__ == "__main__":
    # Example usage
    plot_weak_scaling("mpi_weak_averaged_results_smallpayloads.csv")