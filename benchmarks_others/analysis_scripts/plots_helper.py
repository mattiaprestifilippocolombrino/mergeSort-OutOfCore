import pandas as pd
import matplotlib.pyplot as plt
import sys

# Usage: python plot_results.py results.csv
if len(sys.argv) < 2:
    print("Usage: python plot_results.py <results_file.csv>")
    sys.exit(1)

input_file = sys.argv[1]
df = pd.read_csv(input_file)

# Check required columns
has_speedup = "speedup" in df.columns
has_efficiency = "efficiency" in df.columns

has_time= "avg_time(ms)" in df.columns

# Sort by threads for nicer plotting
if "n_threads" in df.columns:
    df = df.sort_values("n_threads")
else:
    df["n_threads"] = 1  # For sequential case

# Group by payload size & records number
for (payload, records), group in df.groupby(["max_payload_size", "records_number"]):
    plt.figure(figsize=(8, 5))
    if has_time:
    # Plot avg time
        plt.plot(group["n_threads"], group["avg_time(ms)"], marker="o", label="Avg Time")
        plt.title(f"Payload: {payload}, Records: {records}")
        plt.xlabel("Number of Threads")
        plt.grid(True, linestyle="--", alpha=0.6)
        plt.legend()
        plt.tight_layout()

        # Save each plot to a file
        plot_filename = f"time_payload{payload}_records{records}.png"
        plt.savefig(plot_filename)
        print(f"Saved: {plot_filename}")
        plt.close()

    if has_speedup:
        plt.plot(group["n_threads"], group["speedup"], marker="s", label="Speedup")
        plt.title(f"Payload: {payload}, Records: {records}")
        plt.xlabel("Number of Threads")
        plt.grid(True, linestyle="--", alpha=0.6)
        plt.legend()
        plt.tight_layout()

        # Save each plot to a file
        plot_filename = f"speedup_payload{payload}_records{records}.png"
        plt.savefig(plot_filename)
        print(f"Saved: {plot_filename}")
        plt.close()
        
        plt.plot(group["n_threads"], group["efficiency"], marker="^", label="Efficiency")
        plt.title(f"Payload: {payload}, Records: {records}")
        plt.xlabel("Number of Threads")
        plt.grid(True, linestyle="--", alpha=0.6)
        plt.legend()
        plt.tight_layout()

        # Save each plot to a file
        plot_filename = f"efficiency_payload{payload}_records{records}.png"
        plt.savefig(plot_filename)
        print(f"Saved: {plot_filename}")
        plt.close()