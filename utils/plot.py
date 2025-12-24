import os
import numpy as np
import matplotlib.pyplot as plt

DATA_FILE = os.path.join(os.path.dirname(__file__), "..", "data", "latencies.csv")
PLOTS_DIR = os.path.join(os.path.dirname(__file__), "..", "plots")

def main():
    os.makedirs(PLOTS_DIR, exist_ok=True)
    if not os.path.exists(DATA_FILE):
        print("No data file found at", DATA_FILE)
        return
    vals = []
    with open(DATA_FILE, "r") as f:
        for line in f:
            try:
                vals.append(float(line.strip()))
            except ValueError:
                continue
    if not vals:
        print("No latencies to plot")
        return

    arr = np.array(vals)
    pct = np.percentile(arr, [50, 90, 99])
    print(f"p50={pct[0]:.0f} ns p90={pct[1]:.0f} ns p99={pct[2]:.0f} ns")

    plt.figure(figsize=(8, 4))
    plt.hist(arr, bins=50, color="skyblue", edgecolor="black")
    plt.xlabel("Latency (ns)")
    plt.ylabel("Count")
    plt.title("Latency Histogram")
    plt.tight_layout()
    out_path = os.path.join(PLOTS_DIR, "latency_hist.png")
    plt.savefig(out_path)
    print("Saved", out_path)

if __name__ == "__main__":
    main()
