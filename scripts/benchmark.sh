#!/usr/bin/env bash
# COA Benchmark runner — produces speedup/cache-miss data for all -O levels and thread counts
# Usage: ./scripts/benchmark.sh [build_dir]

BUILD=${1:-build}
RESULTS_DIR="results/$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

SIMS=50000
LAYOUTS=("aos" "soa")
THREADS=(1 2 4 8)
OPT_LEVELS=(O0 O1 O2 O3)

echo "=== F1 PitWall COA Benchmark ==="
echo "Simulations: $SIMS"
echo "Output dir:  $RESULTS_DIR"
echo ""

for OPT in "${OPT_LEVELS[@]}"; do
    BINARY="$BUILD/bench_$OPT"
    if [ ! -f "$BINARY" ]; then
        echo "[SKIP] $BINARY not found"
        continue
    fi
    for LAYOUT in "${LAYOUTS[@]}"; do
        for T in "${THREADS[@]}"; do
            TAG="${OPT}_${LAYOUT}_t${T}"
            echo -n "Running $TAG ... "
            "$BINARY" "$SIMS" "$T" "$LAYOUT" > "$RESULTS_DIR/${TAG}.txt" 2>&1
            echo "done"
        done
    done
done

echo ""
echo "Results written to $RESULTS_DIR/"
echo "Run 'python scripts/plot_speedup.py $RESULTS_DIR' to generate charts."
