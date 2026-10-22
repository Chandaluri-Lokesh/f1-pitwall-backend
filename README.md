# F1 PitWall — Backend

C++17 HTTP API server. Listens on `:8080`. All endpoints are prefixed `/api/`.

## Modules

### DSA — Pit Stop Strategy Optimizer

State space: `(lap, compound, tyre_age)`. Lap time model:

```
t_lap = base_lap_time
      - fuel_effect_per_lap × lap
      + base_delta + deg_linear × age + deg_quadratic × age²
```

Five algorithms run on the same input via `comparison_harness.cpp`:

| Algorithm | File | Data structure | Optimality |
| --- | --- | --- | --- |
| Dynamic Programming | `dp_solver.cpp` | 3-D memoisation array | Optimal |
| Greedy | `greedy_solver.cpp` | Min-heap | Heuristic |
| Branch & Bound | `branch_bound_solver.cpp` | Stack + cost bound | Optimal (pruned) |
| A\* | `graph_solver.cpp` | `priority_queue` + admissible heuristic | Optimal |
| Genetic Algorithm | `genetic_solver.cpp` | Population of pit strategies | Near-optimal |

Driver context (`tyre_mgmt_factor`) and team context (`effective_pit_loss`) are loaded from
SQLite and forwarded in the solve request.

### COA — Monte Carlo Benchmark

Each simulation runs one full race (fixed soft→medium strategy at lap N/2) with per-lap
Gaussian degradation noise and Bernoulli safety-car events.

| Mode | File | Notes |
| --- | --- | --- |
| `single` | `monte_carlo_single.cpp` | AoS and SoA variants; `std::mt19937(42)` |
| `omp_N` | `monte_carlo_omp.cpp` | Per-thread RNG; `reduction(+:sum,sum_sq)`; `schedule(dynamic,64)` |
| `cuda` | `monte_carlo_cuda.cu` | One GPU thread per simulation; `curandState`; per-phase `cudaEvent` timing |

---

## Build

Requires **Visual Studio 2022** with the "Desktop development with C++" workload.

```bash
# CPU-only (default)
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release

# With CUDA (requires CUDA Toolkit ≥ 11.0)
cmake -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release -DENABLE_CUDA=ON

# Compile
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
  build/f1_pitwall_backend.sln /p:Configuration=Release /m
```

CMake targets:

| Target | Description |
| --- | --- |
| `f1_pitwall` | Main server executable |
| `bench_O0` … `bench_O3` | Standalone COA benchmark at each optimisation level |
| `bench_cuda` | COA benchmark with CUDA (only when `ENABLE_CUDA=ON`) |
| `pitwall_tests` | Doctest suite (15 tests) |

---

## Run

```bash
# Start server (serves on :8080, loads pitwall.db from ./data)
./build/Release/f1_pitwall.exe --data ./data

# Run tests
ctest --test-dir build -C Release

# Standalone COA benchmark
./build/Release/bench_O3.exe 100000 8 soa
#                             ^sims  ^threads  ^layout
```

---

## API routes

| Method | Path | Body / params | Response |
| --- | --- | --- | --- |
| GET | `/api/health` | — | `{"status":"ok"}` |
| GET | `/api/drivers` | `?team_id=` (optional) | `Driver[]` |
| GET | `/api/teams` | — | `Team[]` |
| GET | `/api/circuit` | `?circuit_id=&year=` | `CircuitInfo` |
| GET | `/api/dsa/circuits` | — | `[{circuit_id, year}]` |
| POST | `/api/dsa/solve` | `{race, algorithms[], driver?, team?}` | `HarnessResult` |
| POST | `/api/coa/benchmark` | `{race, num_simulations, num_threads, layout, use_gpu?}` | `{results: SimResult[]}` |

---

## Project structure

```
include/
  models/          race_state.h · tyre.h · degradation.h
  dsa/             solver interfaces
  coa/             monte_carlo.h  (SimConfig · SimResult · MemLayout)
src/
  models/          tyre.cpp · degradation.cpp · driver.cpp · team.cpp
  dsa/             dp · greedy · branch_bound · graph (A*) · genetic · harness
  coa/             monte_carlo_single.cpp · monte_carlo_omp.cpp · monte_carlo_cuda.cu
                   bench_main.cpp · profiler.cpp
  api/             server.cpp · routes_dsa.cpp · routes_coa.cpp
  main.cpp
tests/             Doctest suite — test_dsa_solvers · test_degradation · test_monte_carlo · test_db_reader
data/              pitwall.db (SQLite)
third_party/       cpp-httplib · nlohmann/json · sqlite amalgamation
```

---

## Third-party dependencies (vendored, no install required)

- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — header-only HTTP server
- [nlohmann/json](https://github.com/nlohmann/json) — header-only JSON
- [SQLite amalgamation](https://sqlite.org/amalgamation.html) — `sqlite3.h` + `sqlite3.c`
