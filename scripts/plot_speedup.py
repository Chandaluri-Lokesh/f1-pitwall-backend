"""
COA speedup & cache-miss visualization
Usage: python scripts/plot_speedup.py <results_dir>
Reads text outputs from benchmark.sh and generates matplotlib charts.
"""
import sys, os, re
import matplotlib.pyplot as plt

# TODO: parse wall_clock_ms from benchmark output files
# and plot:
#   1. Speedup vs thread count (per opt level, per layout)
#   2. Wall time vs opt level (single-threaded AOS vs SOA)
#   3. Amdahl's Law theoretical overlay

def parse_results(results_dir):
    data = {}
    for fname in os.listdir(results_dir):
        if not fname.endswith(".txt"):
            continue
        tag = fname[:-4]
        path = os.path.join(results_dir, fname)
        with open(path) as f:
            content = f.read()
        m = re.search(r"Single:\s*([\d.]+)\s*ms", content)
        if m:
            data[tag] = float(m.group(1))
    return data

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python plot_speedup.py <results_dir>")
        sys.exit(1)
    data = parse_results(sys.argv[1])
    print(f"Parsed {len(data)} results")
    # TODO: generate charts
    plt.show()
