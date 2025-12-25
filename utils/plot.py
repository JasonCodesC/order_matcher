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

    arr = np.array(vals) / 1_000.0
    pct = np.percentile(arr, [50, 90, 99])
    mean = float(np.mean(arr))
    median = float(np.median(arr))
    print(f"p50={pct[0]:.2f} us p90={pct[1]:.2f} us p99={pct[2]:.2f} us")
    print(f"mean={mean:.2f} us median={median:.2f} us")

    plt.figure(figsize=(8, 4))
    plt.hist(arr, bins=50, color="skyblue", edgecolor="black")
    mean_s = mean / 1_000_000.0
    median_s = median / 1_000_000.0
    plt.axvline(mean, color="red", linestyle="--", linewidth=1.5,
                label=f"Mean {mean:.2f} us ({mean_s:.6f} s)")
    plt.axvline(median, color="green", linestyle="--", linewidth=1.5,
                label=f"Median {median:.2f} us ({median_s:.6f} s)")
    plt.xlabel("Latency (us)")
    plt.ylabel("Count")
    plt.title("Latency Histogram")
    plt.legend()
    plt.tight_layout()
    out_path = os.path.join(PLOTS_DIR, "latency_hist.png")
    plt.savefig(out_path)
    print("Saved", out_path)

if __name__ == "__main__":
    main()
