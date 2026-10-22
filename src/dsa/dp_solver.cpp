#include "dsa/dp_solver.h"
#include "models/degradation.h"
#include <chrono>
#include <limits>
#include <vector>

// Dynamic Programming — Exact Pit Stop Strategy Optimizer
// =========================================================
// State:  (lap, compound_idx, tyre_age)
//         lap        : 1..N — the lap we are ABOUT to drive
//         compound_idx: index into tyres[]
//         tyre_age   : laps already completed on current set when arriving at this lap
//
// Value:  dp[lap][c][a] = minimum total time to finish the race from this state
//
// Transitions (backward, from lap N → lap 1):
//   STAY :  drive lap on (c, a) → cost = lap_time(c, a, lap) + dp[lap+1][c][a+1]
//   PIT→c': pit before lap, drive on (c', 0) → cost = pit_loss
//                                                     + lap_time(c', 0, lap)
//                                                     + dp[lap+1][c'][1]
//           constraint: a >= tyres[c].min_laps  (respect minimum stint)
//                       lap < N                  (never pit on the very last lap)
//
// Reconstruction: follow action[] table forward from the optimal start compound.
//
// Complexity: O(N × C² × A) time,  O(N × C × A) space
// Data structure: 3D array (flat vector, row-major)
// Quality: exact optimum within the model

namespace f1 {
namespace dsa {

StrategyResult DPSolver::solve(const RaceConfig&            config,
                                const std::vector<TyreSpec>& tyres) {
    auto t_start = std::chrono::steady_clock::now();

    StrategyResult result;
    result.algorithm      = name();
    result.data_structure = "3d_array";
    result.states_explored = 0;
    result.stops_count     = 0;

    if (tyres.empty() || config.total_laps <= 0) {
        result.total_race_time = std::numeric_limits<double>::max();
        auto t_end = std::chrono::steady_clock::now();
        result.wall_clock_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        return result;
    }

    const int    N        = config.total_laps;
    const int    C        = static_cast<int>(tyres.size());
    const int    A        = N + 1;   // tyre_age can be 0..N (full race on one set)
    const double INF      = std::numeric_limits<double>::max() / 2.0;
    const double pit_loss = (config.effective_pit_loss > 0.0)
                          ? config.effective_pit_loss
                          : config.pit_loss_seconds;

    // Flat 3D tables: index = (lap * C + c) * A + a
    // lap is 1-based, but we allocate [0..N+1] so offset 0 is unused
    const int SZ = (N + 2) * C * A;
    std::vector<double> dp(SZ, INF);
    std::vector<int>    act(SZ, -2);   // -2 = unvisited, -1 = stay, >=0 = pit to cmp idx

    auto idx = [&](int lap, int c, int a) -> int {
        return (lap * C + c) * A + a;
    };

    // Base case: "lap N+1" means race is over — zero remaining cost
    for (int c = 0; c < C; ++c)
        for (int a = 0; a < A; ++a)
            dp[idx(N + 1, c, a)] = 0.0;

    // ── Backward induction ──────────────────────────────────────────────────
    for (int lap = N; lap >= 1; --lap) {
        for (int c = 0; c < C; ++c) {
            for (int a = 0; a < A - 1; ++a) {   // a+1 must stay in bounds
                ++result.states_explored;

                double best = INF;
                int    best_act = -1;   // -1 = stay

                // Option A — Stay on current tyre (respect max_laps cliff)
                if (a < tyres[c].max_laps || lap == N) {
                    double future = dp[idx(lap + 1, c, a + 1)];
                    if (future < INF) {
                        double cost = lap_time(config, tyres[c], a, lap) + future;
                        if (cost < best) { best = cost; best_act = -1; }
                    }
                }

                // Option B — Pit to compound cp
                // Constraints: a >= min_laps on current,  lap < N (never last lap)
                if (a >= tyres[c].min_laps && lap < N) {
                    for (int cp = 0; cp < C; ++cp) {
                        double future = dp[idx(lap + 1, cp, 1)];
                        if (future >= INF) continue;
                        double cost = pit_loss
                                    + lap_time(config, tyres[cp], 0, lap)
                                    + future;
                        if (cost < best) { best = cost; best_act = cp; }
                    }
                }

                dp[idx(lap, c, a)]  = best;
                act[idx(lap, c, a)] = best_act;
            }
        }
    }

    // ── Find optimal starting compound ─────────────────────────────────────
    // (solver is free to choose which compound to start on)
    double best_total = INF;
    int    start_c    = 0;
    for (int c = 0; c < C; ++c) {
        if (dp[idx(1, c, 0)] < best_total) {
            best_total = dp[idx(1, c, 0)];
            start_c    = c;
        }
    }
    result.total_race_time = best_total;

    // ── Reconstruct path ────────────────────────────────────────────────────
    int cur_c = start_c;
    int cur_a = 0;
    for (int lap = 1; lap <= N; ++lap) {
        int a = act[idx(lap, cur_c, cur_a)];
        if (a >= 0) {
            // Pit to compound a before this lap
            result.stops.push_back({ lap, tyres[a].compound });
            cur_c = a;
            cur_a = 1;   // drove 1 lap on fresh tyre (this lap at age 0)
        } else {
            ++cur_a;
        }
    }

    result.stops_count = static_cast<int>(result.stops.size());

    auto t_end = std::chrono::steady_clock::now();
    result.wall_clock_ms =
        std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
}

} // namespace dsa
} // namespace f1
