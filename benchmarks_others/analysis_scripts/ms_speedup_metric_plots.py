import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import argparse
import os
import sys

def drop_max_and_average(df, group_cols):
    def drop_max(group):
        if len(group) > 1:
            group = group.drop(group['time(ms)'].idxmax())
        return pd.Series({'time(ms)': group['time(ms)'].mean()})
    return df.groupby(group_cols, as_index=False).apply(drop_max)

def load_and_average(parallel_csv, sequential_csv):
    try:
        # Load CSVs and drop rows with missing time
        df_par = pd.read_csv(parallel_csv).dropna(subset=['time(ms)'])
        df_seq = pd.read_csv(sequential_csv).dropna(subset=['time(ms)'])
    except Exception as e:
        print(f"Error reading CSV files: {e}")
        sys.exit(1)

    # Average parallel times after dropping max
    par_grouped = drop_max_and_average(
        df_par, ['max_payload_size', 'records_number', 'n_threads']
    )

    # Average sequential times after dropping max
    seq_grouped = drop_max_and_average(
        df_seq, ['max_payload_size', 'records_number']
    ).rename(columns={'time(ms)': 'seq_time(ms)'})

    # Merge parallel and sequential data
    merged = pd.merge(
        par_grouped, seq_grouped,
        on=['max_payload_size', 'records_number'],
        how='outer'
    )

    # Compute Speedup and Efficiency
    merged['speedup'] = merged['seq_time(ms)'] / merged['time(ms)']
    merged['efficiency'] = merged['speedup'] / merged['n_threads']

    return merged

def plot_curves(merged, output_dir, show):
    sns.set(style="whitegrid")
    unique_configs = merged[['max_payload_size', 'records_number']].drop_duplicates()

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    for _, row in unique_configs.iterrows():
        payload = row['max_payload_size']
        records = row['records_number']

        subset = merged[(merged['max_payload_size'] == payload) &
                        (merged['records_number'] == records)]
        print(subset)
        


        plt.figure(figsize=(10, 5))

        # Plot Speedup
        plt.subplot(1, 2, 1)
        plt.title(f"Speedup - Records: {records}, Payload: {payload}")
        plt.plot(subset['n_threads'], subset['speedup'], marker='o', color='b')
        plt.xlabel("Number of Threads")
        plt.ylabel("Speedup")
        plt.xticks(subset['n_threads'])
        plt.grid(True)

        # Plot Efficiency
        plt.subplot(1, 2, 2)
        plt.title(f"Efficiency - Records: {records}, Payload: {payload}")
        plt.plot(subset['n_threads'], subset['efficiency'], marker='o', color='g')
        plt.xlabel("Number of Threads")
        plt.ylabel("Efficiency")
        plt.xticks(subset['n_threads'])
        plt.grid(True)

        plt.tight_layout()
        filename = f"speedup_efficiency_records_{records}_payload_{payload}.png"
        filepath = os.path.join(output_dir, filename)
        plt.savefig(filepath)
        print(f"Saved plot to {filepath}")

        if show:
            plt.show()
        else:
            plt.close()

def main():
    parser = argparse.ArgumentParser(description="Compute and plot speedup/efficiency from parallel and sequential runs (dropping max before averaging).")
    parser.add_argument("parallel_csv", help="CSV file from parallel runs.")
    parser.add_argument("sequential_csv", help="CSV file from sequential runs.")
    parser.add_argument("--output-dir", default="plots", help="Directory to save plots (default: 'plots').")
    parser.add_argument("--show", action="store_true", help="Show plots interactively after saving.")
    args = parser.parse_args()

    merged = load_and_average(args.parallel_csv, args.sequential_csv)
    plot_curves(merged, args.output_dir, args.show)

if __name__ == "__main__":
    main()