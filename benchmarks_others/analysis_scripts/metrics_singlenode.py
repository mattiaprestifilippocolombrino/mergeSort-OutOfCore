import pandas as pd

def aggregate_remove_max(df, group_cols):
    result_rows = []
    for group_keys, group_df in df.groupby(group_cols):
        group_df = group_df.drop(group_df["time(ms)"].idxmax())  # remove max
        avg_time = (group_df["time(ms)"]/1000).mean()
        result_rows.append(dict(zip(group_cols, group_keys), avg_time=avg_time))
    return pd.DataFrame(result_rows)

# -------------------------
# Input files
parallel_file = "result_omp_ooc.out"
sequential_file = "result_seq_ooc.out"
output_file = "omp_averaged_speedup_efficiency.csv"

# Read both CSVs
df_parallel = pd.read_csv(parallel_file)
df_sequential = pd.read_csv(sequential_file)

# Aggregate parallel
parallel_group_cols = ["n_threads", "max_payload_size", "records_number"]
df_parallel_agg = aggregate_remove_max(df_parallel, parallel_group_cols)

# Aggregate sequential
# Assume sequential has no n_threads column → add one with value 1
if "n_threads" not in df_sequential.columns:
    df_sequential["n_threads"] = 1
sequential_group_cols = ["n_threads", "max_payload_size", "records_number"]
df_sequential_agg = aggregate_remove_max(df_sequential, sequential_group_cols)

# Merge aggregated results
merged = pd.merge(
    df_parallel_agg,
    df_sequential_agg.rename(columns={"avg_time": "sequential_time"}),
    on=["max_payload_size", "records_number"],
    suffixes=("", "_seq")
)

# Calculate speedup & efficiency
merged["speedup"] = merged["sequential_time"] / merged["avg_time"]
merged["efficiency"] = merged["speedup"] / merged["n_threads"]

# Save output
merged.to_csv(output_file, index=False)

print(f"Results with speedup and efficiency saved to {output_file}")

