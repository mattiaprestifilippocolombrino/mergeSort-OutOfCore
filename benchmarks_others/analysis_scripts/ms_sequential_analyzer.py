import matplotlib.pyplot as plt
import pandas as pd
import argparse
import os
import numpy as np

def analyze_csv(input_file, save_plots=False, output_dir="plots"):
    # Read CSV
    df = pd.read_csv(input_file)

    # Function to remove max value in each group
    def remove_max_and_agg(group):
        filtered = group["time(ms)"][group["time(ms)"] != group["time(ms)"].max()]
        # If after removing max there's no data left, fallback to original group to avoid empty aggregation
        if filtered.empty:
            filtered = group["time(ms)"]
        return pd.Series({
            "mean": filtered.mean(),
            "var": filtered.var()
        })

    # Group by and apply the filtering+aggregation
    stats = df.groupby(["max_payload_size", "records_number"]).apply(remove_max_and_agg).reset_index()

    print("\nMean and Variance of time(ms) for each (max_payload_size, records_number) excluding max value:")
    print(stats)

    # Create output directory if needed
    if save_plots:
        os.makedirs(output_dir, exist_ok=True)

    # Get unique values for x-axis and grouping
    payload_sizes = sorted(stats["max_payload_size"].unique())
    record_numbers = sorted(stats["records_number"].unique())

    bar_width = 0.15  # Width of each bar
    x = np.arange(len(payload_sizes))  # Positions for groups

    # --- Plot Mean ---
    plt.figure(figsize=(12, 6))
    for idx, records_number in enumerate(record_numbers):
        means = stats[stats["records_number"] == records_number]["mean"]
        plt.bar(
            x + idx * bar_width, means,
            width=bar_width,
            label=f"Records: {records_number}"
        )

    plt.title("Mean Time (ms) per Max Payload Size and Records Number (max excluded)")
    plt.xlabel("Max Payload Size")
    plt.ylabel("Mean Time (ms)")
    plt.xticks(x + bar_width * (len(record_numbers) - 1) / 2, payload_sizes)
    plt.legend(title="Records Number")
    plt.grid(axis='y', linestyle='--', alpha=0.7)

    if save_plots:
        mean_plot_path = os.path.join(output_dir, "mean_time_bar_excl_max.png")
        plt.savefig(mean_plot_path)
        print(f"Saved mean bar plot to {mean_plot_path}")
    else:
        plt.show()

    # --- Plot Variance ---
    plt.figure(figsize=(12, 6))
    for idx, records_number in enumerate(record_numbers):
        variances = stats[stats["records_number"] == records_number]["var"]
        plt.bar(
            x + idx * bar_width, variances,
            width=bar_width,
            label=f"Records: {records_number}"
        )

    plt.title("Variance of Time (ms²) per Max Payload Size and Records Number (max excluded)")
    plt.xlabel("Max Payload Size")
    plt.ylabel("Variance (ms²)")
    plt.xticks(x + bar_width * (len(record_numbers) - 1) / 2, payload_sizes)
    plt.legend(title="Records Number")
    plt.grid(axis='y', linestyle='--', alpha=0.7)

    if save_plots:
        var_plot_path = os.path.join(output_dir, "variance_time_bar_excl_max.png")
        plt.savefig(var_plot_path)
        print(f"Saved variance bar plot to {var_plot_path}")
    else:
        plt.show()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Analyze CSV and plot mean/variance of time(ms) per iteration.")
    parser.add_argument("--input", "-i", required=True, help="Path to the input CSV file.")
    parser.add_argument("--save-plots", "-s", action="store_true", help="Save plots instead of showing them.")
    parser.add_argument("--output-dir", "-o", default="plots", help="Directory to save plots (default: ./plots)")

    args = parser.parse_args()

    analyze_csv(args.input, save_plots=args.save_plots, output_dir=args.output_dir)
