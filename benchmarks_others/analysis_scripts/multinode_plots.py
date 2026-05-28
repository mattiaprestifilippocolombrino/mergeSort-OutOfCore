# Read the CSV
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("mpi_strong_averaged_results.csv")

# Find unique (payload, records) combinations
combos = df[['max_payload_size', 'records_number']].drop_duplicates()

# Plot for each combination
for _, combo in combos.iterrows():
    payload = combo['max_payload_size']
    records = combo['records_number']
    subset = df[(df['max_payload_size'] == payload) & (df['records_number'] == records)]
    
    plt.figure(figsize=(8, 6))
    for threads in sorted(subset['num_threads'].unique()):
        sub = subset[subset['num_threads'] == threads].sort_values('num_nodes')
        plt.plot(sub['num_nodes'], sub['avg_time(ms)'],
                 marker='o', label=f"{threads} threads")
    
    plt.title(f"Payload={payload}, Records={records}")
    plt.xlabel("Number of Nodes")
    plt.ylabel("Average Time (s)")
    plt.legend(title="Threads")
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(f"mpi_avgtime_mp{payload}_nr{records}")