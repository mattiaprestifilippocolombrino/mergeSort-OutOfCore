import pandas as pd

# Load sequential and parallel CSVs
seq_df = pd.read_csv("sequential_averaged_results.csv")
par_df = pd.read_csv("mpi_strong_averaged_results.csv")

# Merge them on matching experiment parameters (excluding threads/nodes)
# Adjust "on=" list if your CSV has more identifying columns
merged = pd.merge(
    par_df,
    seq_df,
    on=["max_payload_size", "records_number"],
    suffixes=("", "_seq")
)

# Calculate speedup and efficiency
merged["speedup"] = merged["avg_time(ms)_seq"] / merged["avg_time(ms)"]
merged["efficiency"] = merged["speedup"] / (merged["num_threads"] * merged["num_nodes"])

# Save results
merged.to_csv("speedup_efficiency.csv", index=False)

print(merged[[
    "num_threads", "num_nodes",
    "max_payload_size", "records_number",
    "avg_time(ms)", "avg_time(ms)_seq",
    "speedup", "efficiency"
]])