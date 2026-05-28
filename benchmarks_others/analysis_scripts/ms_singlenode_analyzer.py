import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import argparse
import os
import sys

def load_and_process(csv_file):
    try:
        df = pd.read_csv(csv_file)
    except Exception as e:
        print(f"Error reading CSV: {e}")
        sys.exit(1)

    # Drop rows where time(ms) is missing
    df = df.dropna(subset=['time(ms)'])

    # Average over iterations with same parameters
    grouped = df.groupby(
        ['iteration', 'max_payload_size', 'records_number', 'n_threads'],
        as_index=False
    ).mean()

    # Now, for each unique (max_payload_size, records_number, n_threads),
    # drop the row with the maximum time, then compute mean and variance
    def drop_max_and_stats(group):
        if len(group) > 1:  # only drop max if more than one sample
            group = group.drop(group['time(ms)'].idxmax())
        return pd.Series({
            'time_mean': group['time(ms)'].mean(),
            'time_var': group['time(ms)'].var(ddof=0)  # population variance
        })

    averaged = grouped.groupby(
        ['max_payload_size', 'records_number', 'n_threads'],
        as_index=False
    ).apply(drop_max_and_stats)

    return averaged

def plot_data(averaged, output_dir, show):
    sns.set(style="whitegrid")
    unique_configs = averaged[['max_payload_size', 'records_number']].drop_duplicates()

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    for _, row in unique_configs.iterrows():
        payload = row['max_payload_size']
        records = row['records_number']

        subset = averaged[(averaged['max_payload_size'] == payload) &
                          (averaged['records_number'] == records)]

        print(subset)

        plt.figure(figsize=(8, 6))
        plt.title(f"Records: {records}, Max Payload Size: {payload}")
        plt.plot(subset['n_threads'],subset['time_mean'])
        plt.xlabel("Number of Threads")
        plt.ylabel("Average Time (ms)")
        plt.xticks(subset['n_threads'])
        plt.grid(True)
        plt.tight_layout()

        filename = f"plot_records_{records}_payload_{payload}.png"
        filepath = os.path.join(output_dir, filename)
        plt.savefig(filepath)
        print(f"Saved plot to {filepath}")

        if show:
            plt.show()
        else:
            plt.close()

def main():
    parser = argparse.ArgumentParser(description="Average CSV data and plot graphs for each configuration.")
    parser.add_argument("csv_file", help="Path to the CSV file.")
    parser.add_argument("--output-dir", default="plots", help="Directory to save plots (default: 'plots').")
    parser.add_argument("--show", action="store_true", help="Show plots interactively after saving.")
    args = parser.parse_args()

    averaged = load_and_process(args.csv_file)
    plot_data(averaged, args.output_dir, args.show)

if __name__ == "__main__":
    main()
