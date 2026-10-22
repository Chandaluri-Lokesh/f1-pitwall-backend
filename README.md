# F1 PitWall — Backend

> C++17 pit-stop strategy optimizer and race simulation engine with an HTTP API.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C?logo=cmake)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey?logo=windows)
![Tests](https://img.shields.io/badge/tests-15%2F15%20passing-brightgreen)
![License](https://img.shields.io/badge/license-MIT-green)

---

## Overview

F1 PitWall models the pit-stop strategy problem as a finite-horizon shortest-path search over the state space `(lap, compound, tyre_age)`. Five algorithms—ranging from exact DP to a genetic heuristic—are run on identical inputs and compared for solution quality and wall time. A Monte Carlo engine then stress-tests the optimal strategy under stochastic tyre degradation and safety-car events.

The backend exposes a JSON HTTP API (port `8080`) consumed by the [React frontend](https://github.com/Chandaluri-Lokesh/f1-pitwall-frontend).

---

## Features

- **5 DSA solvers** compared head-to-head on every solve request
- **Driver & team context** — `tyre_mgmt_factor` and `effective_pit_loss` loaded from SQLite
- **Monte Carlo simulation** — AoS, SoA, and OpenMP parallel variants (up to 100k sims / 8 threads)
- **CUDA variant** (optional) — one GPU thread per simulation with per-phase `cudaEvent` timing
- **SQLite data layer** — 12 circuits × 2 seasons, 23 drivers, 12 teams
- **Vendored dependencies** — zero install required beyond Visual Studio 2022

---

## Algorithms

| Algorithm | File | Data Structure | Optimality |
|---|---|---|---|
| Dynamic Programming | `dp_solver.cpp` | 3-D memoisation array (flat `vector`) | Optimal |
| Greedy | `greedy_solver.cpp` | Min-heap (`priority_queue`) | Heuristic |
| Branch & Bound | `branch_bound_solver.cpp` | Recursion stack (DFS) + cost bound | Optimal (pruned) |
| A* / Dijkstra | `graph_solver.cpp` | Binary heap + admissible heuristic | Optimal |
| Genetic Algorithm | `genetic_solver.cpp` | Population `vector` of pit strategies | Near-optimal |

DP, B&B, and A* solutions agree within 1×10⁻⁴ s on all test circuits.

---

## Lap Time Model

```
t_lap = base_lap_time
      - fuel_effect_per_lap × lap
      + base_delta
      + deg_linear × age
      + deg_quadratic × age²
```

Parameters are per-compound and per-circuit, sourced from FastF1 telemetry via `scripts/pipeline.py`.

---

## Monte Carlo Simulation

Each simulation runs one full race (fixed soft → medium strategy at lap N/2) with:
- Per-lap **Gaussian degradation noise** (`σ = 0.3 s`)
- **Bernoulli safety-car events** — `P = 0.05/lap`, `+15 s` time penalty

| Mode | File | Notes |
|---|---|---|
| `single` | `monte_carlo_single.cpp` | AoS and SoA layouts; `std::mt19937(42)` |
| `omp_N` | `monte_carlo_omp.cpp` | Per-thread RNG; `reduction(+:sum,sum_sq)`; `schedule(dynamic,64)` |
| `cuda` | `monte_carlo_cuda.cu` | One GPU thread/sim; `curandState`; `cudaEvent` timing |

Typical results (100k sims, 4 threads): AoS **3.5×** speedup, SoA **4.2×** speedup.

---

## Build

Requires **Visual Studio 2022** with the "Desktop development with C++" workload.

```bash
# Configure (CPU-only)
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release

# Configure (with CUDA — requires CUDA Toolkit >= 11.0)
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON

# Build all targets
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  build/f1_pitwall_backend.sln /p:Configuration=Release /m
```

### CMake Targets

| Target | Description |
|---|---|
| `f1_pitwall` | Main API server (`build/Release/f1_pitwall.exe`) |
| `bench_O0` … `bench_O3` | Standalone COA benchmark at each optimisation level |
| `bench_cuda` | COA benchmark with CUDA (requires `ENABLE_CUDA=ON`) |
| `pitwall_tests` | Doctest suite — 15 tests |

---

## Data Pipeline

```bash
# Fetch telemetry for 6 circuits, 2 seasons (~5 min, cached after first run)
python scripts/pipeline.py --circuit monza monaco silverstone spa suzuka bahrain --year 2023 2024
```

Produces `data/pitwall.db` (SQLite) and JSON files under `data/circuits/`, `data/sessions/`, `data/tyres/`, `data/drivers/`.

---

## Run

```bash
# Start server (loads pitwall.db from ./data, listens on :8080)
./build/Release/f1_pitwall.exe 8080 data

# Run all tests
./build/Release/pitwall_tests.exe

# Standalone COA benchmark
./build/Release/bench_O3.exe 100000 8 soa
#                             ^sims  ^threads  ^layout
```

---

## API Reference

| Method | Path | Params / Body | Response |
|---|---|---|---|
| `GET` | `/api/health` | — | `{"status":"ok"}` |
| `GET` | `/api/drivers` | `?team_id=` (optional) | `Driver[]` |
| `GET` | `/api/teams` | — | `Team[]` |
| `GET` | `/api/circuit` | `?circuit_id=&year=` | `CircuitInfo` |
| `GET` | `/api/dsa/circuits` | — | `[{circuit_id, year}]` |
| `POST` | `/api/dsa/solve` | `{race, algorithms[], driver?, team?}` | `HarnessResult` |
| `POST` | `/api/coa/benchmark` | `{race, num_simulations, num_threads, layout, use_gpu?}` | `{results: SimResult[]}` |

---

## Project Structure

```
include/
  models/       race_state.h  tyre.h  degradation.h  driver.h  team.h
  dsa/          solver_base.h  dp  greedy  branch_bound  graph  genetic  harness
  coa/          monte_carlo.h  profiler.h
  api/          server.h
src/
  models/       tyre  degradation  driver  team  race_state
  dsa/          dp  greedy  branch_bound  graph(A*)  genetic  comparison_harness
  coa/          monte_carlo_single  monte_carlo_omp  monte_carlo_cuda  bench_main  profiler
  api/          server  routes_dsa  routes_coa
  main.cpp
tests/          test_dsa_solvers  test_degradation  test_monte_carlo  test_db_reader  test_main
scripts/        pipeline.py  benchmark.sh  plot_speedup.py  verify_fastf1.py
data/           circuits/  sessions/  tyres/  drivers/  (pitwall.db — gitignored, regenerate via pipeline)
third_party/    cpp-httplib  nlohmann/json  sqlite3 amalgamation
```

---

## Third-Party Dependencies (vendored)

| Library | Version | License |
|---|---|---|
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.15.x | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.x | MIT |
| [SQLite Amalgamation](https://sqlite.org/amalgamation.html) | 3.47.2 | Public Domain |

---

## Related

- **Frontend:** [f1-pitwall-frontend](https://github.com/Chandaluri-Lokesh/f1-pitwall-frontend) — React + Vite + Tailwind UI
