#include "dsa/branch_bound_solver.h"
#include "models/degradation.h"
#include <algorithm>
#include <chrono>
#include <limits>

// Branch & Bound — Exact Pit Stop Solver
// ========================================
// Explores the strategy tree depth-first (DFS).
//
// At each node (lap, compound, tyre_age) two branches exist:
//   STAY : drive lap on current tyre → recurse (lap+1, compound, tyre_age+1)
//   PIT→c': for each compound c' → recurse (lap+1, c', 1)  [if constraints met]
//
// "Stay" is tried first so a complete feasible solution is found immediately
// (the no-stop strategy), giving a tight upper bound early.  All subsequent
// branches are pruned when their lower bound meets or exceeds that bound.
//
// Lower bound (same admissible heuristic as A*):
//   lb_remaining = remaining * (base + best_delta) - fuel_effect * Σl (l=lap..N)
//
// Pruning condition: cost_so_far + lb_remaining  >=  best.total_race_time
//
// Complexity: O(C^K * N) worst case where K = max stops, but heavy pruning
//             means explored states << DP's O(N*C*A) in typical cases.
// Data structure: implicit recursion stack (no explicit container)
// Quality: exact optimum

namespace f1 {
namespace dsa {

// Admissible lower bound on the remaining cost from lap `lap` to end of race.
// Assumes fresh fastest tyre every lap + full fuel benefit — never overestimates.
static double lb_remaining(const RaceConfig&            config,
                            const std::vector<TyreSpec>& tyres,
                            int                          lap) {
    int remaining = config.total_laps - lap + 1;
    if (remaining <= 0) return 0.0;

    double best_delta = 0.0;
    for (const auto& t : tyres)
        best_delta = std::min(best_delta, t.base_lap_time_delta);

    // Σl for l = lap..N = remaining * (lap + N) / 2
    double lap_sum = static_cast<double>(remaining) *
                     static_cast<double>(lap + config.total_laps) / 2.0;

    return remaining * (config.base_lap_time + best_delta)
         - config.fuel_effect_per_lap * lap_sum;
}

// ── Recursive worker ────────────────────────────────────────────────────────
void BranchBoundSolver::branch(const RaceConfig&            config,
                                const std::vector<TyreSpec>& tyres,
                                int                          lap,
                                Compound                     compound,
                                int                          tyre_age,
                                double                       cost_so_far,
                                std::vector<PitStop>&        current_stops,
                                StrategyResult&              best) {
    // ── Base case: race complete ─────────────────────────────────────────────
    if (lap > config.total_laps) {
        if (cost_so_far < best.total_race_time) {
            best.total_race_time = cost_so_far;
            best.stops           = current_stops;
            best.stops_count     = static_cast<int>(current_stops.size());
        }
        return;
    }

    // ── Bounding: prune if even the optimistic remaining cost can't beat best ─
    double bound = cost_so_far + lb_remaining(config, tyres, lap);
    if (bound >= best.total_race_time) return;

    ++best.states_explored;

    // Find the TyreSpec for the current compound
    int cur_idx = 0;
    for (int i = 0; i < static_cast<int>(tyres.size()); ++i)
        if (tyres[i].compound == compound) { cur_idx = i; break; }

    const TyreSpec& cur_tyre = tyres[cur_idx];

    // ── Branch 1: STAY on current tyre (respect max_laps cliff)
    if (tyre_age < cur_tyre.max_laps || lap == config.total_laps) {
        double lt      = lap_time(config, cur_tyre, tyre_age, lap);
        branch(config, tyres,
               lap + 1, compound, tyre_age + 1,
               cost_so_far + lt,
               current_stops, best);
    }

    // ── Branch 2: PIT to each compound ─────────────────────────────────────
    // Constraints: must have done at least min_laps on current tyre;
    //              never pit on the very last lap.
    if (tyre_age >= cur_tyre.min_laps && lap < config.total_laps) {
        for (int cp = 0; cp < static_cast<int>(tyres.size()); ++cp) {
            double lt_new = lap_time(config, tyres[cp], 0, lap);
            double new_cost = cost_so_far
                            + (config.effective_pit_loss > 0.0
                               ? config.effective_pit_loss
                               : config.pit_loss_seconds)
                            + lt_new;

            // Fast prune before recursing
            if (new_cost + lb_remaining(config, tyres, lap + 1) >= best.total_race_time)
                continue;

            current_stops.push_back({ lap, tyres[cp].compound });
            branch(config, tyres,
                   lap + 1, tyres[cp].compound, 1,
                   new_cost,
                   current_stops, best);
            current_stops.pop_back();
        }
    }
}

// ── Entry point ─────────────────────────────────────────────────────────────
StrategyResult BranchBoundSolver::solve(const RaceConfig&            config,
                                         const std::vector<TyreSpec>& tyres) {
    auto t_start = std::chrono::steady_clock::now();

    StrategyResult best;
    best.algorithm       = name();
    best.data_structure  = "recursion_stack";
    best.total_race_time = std::numeric_limits<double>::max();
    best.stops_count     = 0;
    best.states_explored = 0;

    if (tyres.empty() || config.total_laps <= 0) {
        best.total_race_time = std::numeric_limits<double>::max();
        auto t_end = std::chrono::steady_clock::now();
        best.wall_clock_ms =
            std::chrono::duration<double, std::milli>(t_end - t_start).count();
        return best;
    }

    // Warm-start: one-stint upper bound for each starting compound so pruning
    // kicks in immediately on the first real DFS path
    for (const auto& t : tyres) {
        double no_stop = stint_cost(config, t, 1, 0, config.total_laps);
        if (no_stop < best.total_race_time) best.total_race_time = no_stop;
    }

    std::vector<PitStop> current_stops;

    // Try each starting compound
    for (const auto& t : tyres) {
        current_stops.clear();
        branch(config, tyres,
               1, t.compound, 0, 0.0,
               current_stops, best);
    }

    auto t_end = std::chrono::steady_clock::now();
    best.wall_clock_ms =
        std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return best;
}

} // namespace dsa
} // namespace f1
