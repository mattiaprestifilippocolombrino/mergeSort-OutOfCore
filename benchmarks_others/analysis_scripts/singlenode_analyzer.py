import pandas as pd

# Input/output file paths
input_file = "result_omp_ooc.out"
output_file = "omp_averaged_results.csv"

# Read CSV
df = pd.read_csv(input_file)

# Group by n_threads, max_payload_size, records_number
result_rows = []
group_cols = ["num_threads", "max_payload_size", "records_number"]

for group_keys, group_df in df.groupby(group_cols):
    # Drop the row with the maximum time(ms)
    group_df = group_df.drop(group_df["time(ms)"].idxmax())
    
    # Compute the average of remaining times
    avg_time = (group_df["time(ms)"]/1000).mean()
    std_time = (group_df['time(ms)']/1000).std()
    
    result_rows.append({
        "num_threads": group_keys[0],
        "max_payload_size": group_keys[1],
        "records_number": group_keys[2],
        "avg_time(ms)": avg_time,
        'std_time(ms)': std_time
    })

# Convert to DataFrame and save
result_df = pd.DataFrame(result_rows)
result_df.to_csv(output_file, index=False)

print(f"Aggregated results saved to {output_file}")