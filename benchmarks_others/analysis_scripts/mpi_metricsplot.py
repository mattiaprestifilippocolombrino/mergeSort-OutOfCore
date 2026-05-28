import pandas as pd
import matplotlib.pyplot as plt

# Load CSV file
df = pd.read_csv("mpi_speedup_efficiency.csv")

# Group by max_payload_size and records_number
grouped = df.groupby(["max_payload_size", "records_number"])

for (payload, records), group in grouped:
    # Sort so lines connect properly
    group = group.sort_values(by=["num_nodes"])

    # === Plot efficiency ===
    plt.figure()
    for threads, sub in group.groupby("num_threads"):
        plt.plot(
            sub["num_nodes"], 
            sub["efficiency"], 
            marker="o", 
            label=f"{threads} threads"
        )
    plt.xlabel("Number of Nodes")
    plt.ylabel("Efficiency")
    plt.title(f"Efficiency - payload={payload}, records={records}")
    plt.grid(True)
    plt.legend()
    plt.savefig(f"efficiency_payload{payload}_records{records}.png", dpi=300)
    plt.close()

    # === Plot speedup ===
    plt.figure()
    for threads, sub in group.groupby("num_threads"):
        plt.plot(
            sub["num_nodes"], 
            sub["speedup"], 
            marker="o", 
            label=f"{threads} threads"
        )
    plt.xlabel("Number of Nodes")
    plt.ylabel("Speedup")
    plt.title(f"Speedup - payload={payload}, records={records}")
    plt.grid(True)
    plt.legend()
    plt.savefig(f"speedup_payload{payload}_records{records}.png", dpi=300)
    plt.close()