#include "dsa/graph_solver.h"
#include "models/degradation.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <vector>

// A* / Dijkstra Pit Stop Solver
// ==============================
// Models the race as a DAG where each node is a state (lap, compound, tyre_age)
// and edges are either "stay" or "pit to compound c'".
//
// A*:      f(n) = g(n) + h(n)   — h is an admissible lower bound on remaining cost
// Dijkstra: f(n) = g(n)          — same algorithm with h=0
//
// Both find the exact optimum.  A* typically expands fewer nodes (better for
// academic comparison of states_explored vs DP and B&B).
//
// State packing:  key = (lap * C + compound_idx) * A + tyre_age
//                 lap in [1..N+1],  compound_idx in [0..C),  tyre_age in [0..N]
//
// Data structure: binary min-heap (std::priority_queue with greater<>)

namespace f1 {
namespace dsa {

// ── Admissible heuristic ────────────────────────────────────────────────────
// Lower bound on remaining time from state s:
//   sum over laps s.lap..N of (base + best_delta - fuel_gain_on_lap_l)
// = remaining * (base + best_delta)  -  fuel_effect * Σl  (l = s.lap..N)
// No pit penalty is included, so this never overestimates.
double GraphSolver::heuristic(const RaceConfig&            config,
                               const std::vector<TyreSpec>& tyres,
                               const StrategyState&         state) const {
    int remaining = config.total_laps - state.lap + 1;  // laps left incl. current
    if (remaining <= 0) return 0.0;

    double best_delta = 0.0;
    for (const auto& t : tyres)
        best_delta = std::min(best_delta, t.base_lap_time_delta);

    // Arithmetic sum: Σl for l = state.lap to N  =  remaining * (state.lap + N) / 2
    double lap_sum = static_cast<double>(remaining) *
                     static_cast<double>(state.lap + config.total_laps) / 2.0;

    return remaining * (config.base_lap_time + best_delta)
         - config.fuel_effect_per_lap * lap_sum;
}

// ── Main search ─────────────────────────────────────────────────────────────
StrategyResult GraphSolver::solve(const RaceConfig&            config,
                                   const std::vector<TyreSpec>& tyres) {
    auto t_start = std::chrono::steady_clock::now();

    StrategyResult result;
    result.algorithm      = name();
    result.data_structure = "binary_heap";
    result.states_explored = 0;
    result.stops_count     = 0;

    if (tyres.empty() || config.total_laps <= 0) {
        result.total_race_time = std::numeric_limits<double>::max();
        auto t_end = std::chrono::steady_clock::now();
        result.wall_clock_ms =
            std::chrono::duration<double, std::milli>(t_end - t_start).count();
        return result;
    }

    const int    N        = config.total_laps;
    const int    C        = static_cast<int>(tyres.size());
    const int    A        = N + 1;
    const int    GOAL_LAP = N + 1;
    const double pit_loss = (config.effective_pit_loss > 0.0)
                          ? config.effective_pit_loss
                          : config.pit_loss_seconds;

    auto pack = [&](int lap, int c, int a) -> int {
        return (lap * C + c) * A + a;
    };
    auto unpack = [&](int k, int& lap, int& c, int& a) {
        a   = k % A;  k /= A;
        c   = k % C;
        lap = k / C;
    };

    // Priority queue: (f_cost, g_cost, state_key)  — min-heap on f_cost
    using Entry = std::tuple<double, double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    // best_g[key] = lowest g seen; parent[key] = {parent_key, action}
    //   action: -1 = stay / start,  >=0 = pit to that compound index
    const int NO_PARENT = -1;
    std::unordered_map<int, double>             best_g;
    std::unordered_map<int, std::pair<int,int>> par;

    // Seed: start on each compound (g=0, no prior cost)
    for (int c = 0; c < C; ++c) {
        int    k = pack(1, c, 0);
        double h = (algo_ == GraphAlgo::ASTAR)
                 ? heuristic(config, tyres, {1, tyres[c].compound, 0})
                 : 0.0;
        best_g[k] = 0.0;
        par[k]    = { NO_PARENT, -1 };
        pq.push({ h, 0.0, k });
    }

    int    goal_key = -1;
    double goal_g   = std::numeric_limits<double>::max();

    while (!pq.empty()) {
        auto [f, g, k] = pq.top();
        pq.pop();

        // Stale entry: a shorter path to k was already processed
        {
            auto it = best_g.find(k);
            if (it != best_g.end() && g > it->second + 1e-9) continue;
        }

        int lap, c, a;
        unpack(k, lap, c, a);

        ++result.states_explored;

        // ── Goal check ──────────────────────────────────────────────────────
        if (lap == GOAL_LAP) {
            // For A* / Dijkstra, first goal pop is optimal
            goal_g   = g;
            goal_key = k;
            break;
        }

        // ── Successor: Stay ─────────────────────────────────────────────────
        if (a + 1 < A && (a < tyres[c].max_laps || lap == N)) {
            double ng  = g + lap_time(config, tyres[c], a, lap);
            int    nk  = pack(lap + 1, c, a + 1);
            auto   it  = best_g.find(nk);
            if (it == best_g.end() || ng < it->second - 1e-9) {
                best_g[nk] = ng;
                par[nk]    = { k, -1 };
                double h = (algo_ == GraphAlgo::ASTAR)
                         ? heuristic(config, tyres, {lap + 1, tyres[c].compound, a + 1})
                         : 0.0;
                pq.push({ ng + h, ng, nk });
            }
        }

        // ── Successor: Pit to compound cp ───────────────────────────────────
        // Constraints: a >= min_laps,  not the last lap
        if (a >= tyres[c].min_laps && lap < N) {
            for (int cp = 0; cp < C; ++cp) {
                double ng = g + pit_loss + lap_time(config, tyres[cp], 0, lap);
                int    nk = pack(lap + 1, cp, 1);
                auto   it = best_g.find(nk);
                if (it == best_g.end() || ng < it->second - 1e-9) {
                    best_g[nk] = ng;
                    par[nk]    = { k, cp };
                    double h = (algo_ == GraphAlgo::ASTAR)
                             ? heuristic(config, tyres, {lap + 1, tyres[cp].compound, 1})
                             : 0.0;
                    pq.push({ ng + h, ng, nk });
                }
            }
        }
    }

    result.total_race_time = goal_g;

    // ── Reconstruct pit stops ────────────────────────────────────────────────
    // Walk backward from goal_key; collect pit-stop events in reverse order.
    if (goal_key >= 0) {
        std::vector<PitStop> stops;
        int cur = goal_key;
        while (true) {
            auto it = par.find(cur);
            if (it == par.end()) break;
            auto [p, act] = it->second;
            if (p == NO_PARENT) break;

            int lap2, c2, a2;
            unpack(cur, lap2, c2, a2);

            // Pit happened before driving lap (lap2 - 1) i.e. at transition from lap2-1
            if (act >= 0)
                stops.push_back({ lap2 - 1, tyres[act].compound });

            cur = p;
        }
        std::reverse(stops.begin(), stops.end());
        result.stops = std::move(stops);
    }

    result.stops_count = static_cast<int>(result.stops.size());

    auto t_end = std::chrono::steady_clock::now();
    result.wall_clock_ms =
        std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
}

} // namespace dsa
} // namespace f1
