#include "dsa/greedy_solver.h"
#include "models/degradation.h"
#include <chrono>
#include <limits>

// Greedy Pit Stop Strategy
// ========================
// Decision rule (per lap):
//   marginal_cost = lap_time(current_tyre, age) - lap_time(best_fresh_tyre, 0)
//   amortized_pit = effective_pit_loss / remaining_laps
//   Pit if: marginal_cost > amortized_pit
//           AND tyre_age >= compound.min_laps   (respect minimum stint length)
//           AND remaining_laps > 2              (never pit on last 2 laps)
//
// Compound selection: choose the compound whose stint_cost is lowest over
// the remaining distance from the next lap onward.
//
// Paradigm: greedy / heuristic — O(N * C) per decision, O(N) total laps
// Data structure used: none (scalar state only)
// Quality: ~3-8% above DP optimum; fast baseline for comparison harness.

namespace f1 {
namespace dsa {

StrategyResult GreedySolver::solve(const RaceConfig&            config,
                                    const std::vector<TyreSpec>& tyres) {
    auto t_start = std::chrono::steady_clock::now();

    StrategyResult result;
    result.algorithm      = name();
    result.data_structure = "none";
    result.states_explored = 0;
    result.total_race_time = 0.0;
    result.stops_count     = 0;

    if (tyres.empty() || config.total_laps <= 0) {
        result.total_race_time = std::numeric_limits<double>::max();
        auto t_end = std::chrono::steady_clock::now();
        result.wall_clock_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        return result;
    }

    // Effective pit loss: use driver/team adjusted value if set, else circuit default
    double pit_loss = (config.effective_pit_loss > 0.0)
                    ? config.effective_pit_loss
                    : config.pit_loss_seconds;

    // Start on the softest (most negative base_delta = fastest) compound
    int start_idx = 0;
    for (int i = 1; i < (int)tyres.size(); ++i) {
        if (tyres[i].base_lap_time_delta < tyres[start_idx].base_lap_time_delta)
            start_idx = i;
    }

    int    cur_idx        = start_idx;
    int    tyre_age       = 0;   // laps completed on current tyre
    int    laps_on_stint  = 0;   // laps since last pit (for min_laps enforcement)
    double total_time     = 0.0;

    for (int lap = 1; lap <= config.total_laps; ++lap) {
        ++result.states_explored;
        int remaining = config.total_laps - lap + 1;  // including this lap

        // ── Greedy pit decision ──────────────────────────────────────────────
        // Best lap time achievable on any fresh tyre right now
        double best_fresh = std::numeric_limits<double>::max();
        int    best_fresh_idx = 0;
        for (int i = 0; i < (int)tyres.size(); ++i) {
            double lt = lap_time(config, tyres[i], 0, lap);
            if (lt < best_fresh) { best_fresh = lt; best_fresh_idx = i; }
        }

        double current_lt   = lap_time(config, tyres[cur_idx], tyre_age, lap);
        double marginal_cost = current_lt - best_fresh;

        // Amortized pit cost over laps we'd benefit from fresh rubber
        double amortized = (remaining > 1) ? pit_loss / static_cast<double>(remaining - 1)
                                           : std::numeric_limits<double>::max();

        bool past_cliff  = laps_on_stint >= tyres[cur_idx].max_laps;
        bool should_pit  = (marginal_cost > amortized || past_cliff)
                        && laps_on_stint  >= tyres[cur_idx].min_laps
                        && remaining      >  2;

        if (should_pit) {
            // Pick the compound with lowest stint_cost for the remaining distance
            int    best_idx  = best_fresh_idx;
            double best_cost = std::numeric_limits<double>::max();
            int    stint_len = remaining - 1;  // laps after the pit (not counting pit lap)
            for (int i = 0; i < (int)tyres.size(); ++i) {
                if (stint_len < 1) continue;
                double c = stint_cost(config, tyres[i], lap, 0, stint_len);
                if (c < best_cost) { best_cost = c; best_idx = i; }
            }

            // Record the pit stop and pay the time penalty
            result.stops.push_back({ lap, tyres[best_idx].compound });
            total_time   += pit_loss;
            cur_idx       = best_idx;
            tyre_age      = 0;
            laps_on_stint = 0;
        }

        // Drive the lap on current (possibly just-changed) tyre
        total_time += lap_time(config, tyres[cur_idx], tyre_age, lap);
        ++tyre_age;
        ++laps_on_stint;
    }

    result.total_race_time = total_time;
    result.stops_count     = static_cast<int>(result.stops.size());

    auto t_end = std::chrono::steady_clock::now();
    result.wall_clock_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
}

} // namespace dsa
} // namespace f1
