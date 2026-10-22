#include "coa/monte_carlo.h"
#include "models/degradation.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <cmath>

// Monte Carlo Race Simulation — Single-threaded
// ===============================================
// Each simulation runs one full race with stochastic perturbations:
//   - Gaussian degradation noise  (cfg.deg_noise_stddev per lap)
//   - Safety car events           (Bernoulli, cfg.sc_probability per lap → +15s)
//
// Fixed strategy: start on compound 0 (soft), one pit stop at lap N/2
//                 switching to compound 1 (medium).
//
// AOS layout: all per-simulation state packed in one SimStateAOS struct.
//             Hot fields are adjacent → one cache line per live sim.
//
// SOA layout: each field spread across a separate array indexed by sim_idx.
//             Adjacent simulations' same-field values are contiguous in memory,
//             which enables SIMD vectorisation over the sim_idx dimension when
//             the outer loop is swapped (laps outer, sims inner).

namespace f1 {
namespace coa {

// Array-of-Structs layout
struct SimStateAOS {
    double tyre_age;
    double fuel_load;
    double total_time;
    int    current_lap;
    int    compound_idx;
};

// Struct-of-Arrays layout
struct SimStateSOA {
    std::vector<double> tyre_age;
    std::vector<double> fuel_load;
    std::vector<double> total_time;
    std::vector<int>    current_lap;
    std::vector<int>    compound_idx;
};

// ── AOS: one race, state stored as a single compact struct ───────────────────
static double run_one_simulation_aos(const SimConfig&             cfg,
                                      const std::vector<TyreSpec>& tyres,
                                      std::mt19937&                rng) {
    std::normal_distribution<double> deg_noise(0.0, cfg.deg_noise_stddev);
    std::bernoulli_distribution      sc_event(cfg.sc_probability);

    const int    N        = cfg.race.total_laps;
    const double pit_loss = cfg.race.pit_loss_seconds;
    const int    pit_lap  = N / 2;
    const int    pit_cmp  = std::min(1, static_cast<int>(tyres.size()) - 1);

    SimStateAOS s;
    s.compound_idx = 0;
    s.tyre_age     = 0.0;
    s.fuel_load    = static_cast<double>(N);
    s.total_time   = 0.0;
    s.current_lap  = 1;

    for (int lap = 1; lap <= N; ++lap) {
        double sc_delta = sc_event(rng) ? 15.0 : 0.0;

        if (lap == pit_lap && lap < N) {
            s.total_time  += pit_loss
                           + lap_time(cfg.race, tyres[pit_cmp], 0, lap)
                           + deg_noise(rng) + sc_delta;
            s.compound_idx = pit_cmp;
            s.tyre_age     = 1.0;
        } else {
            s.total_time  += lap_time(cfg.race, tyres[s.compound_idx],
                                      static_cast<int>(s.tyre_age), lap)
                           + deg_noise(rng) + sc_delta;
            s.tyre_age    += 1.0;
        }

        s.fuel_load   = static_cast<double>(N - lap);
        s.current_lap = lap + 1;
    }
    return s.total_time;
}

// ── SOA: one race, state read/written through parallel arrays at sim_idx ─────
// The per-simulation state lives in the SOA arrays so that when the caller
// iterates over sim indices in the inner loop, all tyre_age values are laid
// out contiguously — cache-friendly for vectorisation across simulations.
static double run_one_simulation_soa(const SimConfig&             cfg,
                                      const std::vector<TyreSpec>& tyres,
                                      std::mt19937&                rng,
                                      SimStateSOA&                 state,
                                      int                          sim_idx) {
    std::normal_distribution<double> deg_noise(0.0, cfg.deg_noise_stddev);
    std::bernoulli_distribution      sc_event(cfg.sc_probability);

    const int    N        = cfg.race.total_laps;
    const double pit_loss = cfg.race.pit_loss_seconds;
    const int    pit_lap  = N / 2;
    const int    pit_cmp  = std::min(1, static_cast<int>(tyres.size()) - 1);

    state.compound_idx[sim_idx] = 0;
    state.tyre_age[sim_idx]     = 0.0;
    state.fuel_load[sim_idx]    = static_cast<double>(N);
    state.total_time[sim_idx]   = 0.0;
    state.current_lap[sim_idx]  = 1;

    for (int lap = 1; lap <= N; ++lap) {
        double sc_delta = sc_event(rng) ? 15.0 : 0.0;
        int    cmp      = state.compound_idx[sim_idx];
        int    age      = static_cast<int>(state.tyre_age[sim_idx]);

        if (lap == pit_lap && lap < N) {
            state.total_time[sim_idx]   += pit_loss
                                         + lap_time(cfg.race, tyres[pit_cmp], 0, lap)
                                         + deg_noise(rng) + sc_delta;
            state.compound_idx[sim_idx]  = pit_cmp;
            state.tyre_age[sim_idx]      = 1.0;
        } else {
            state.total_time[sim_idx]   += lap_time(cfg.race, tyres[cmp], age, lap)
                                         + deg_noise(rng) + sc_delta;
            state.tyre_age[sim_idx]     += 1.0;
        }

        state.fuel_load[sim_idx]   = static_cast<double>(N - lap);
        state.current_lap[sim_idx] = lap + 1;
    }
    return state.total_time[sim_idx];
}

SimResult run_single(const SimConfig& cfg, const std::vector<TyreSpec>& tyres) {
    auto t_start = std::chrono::steady_clock::now();

    std::mt19937 rng(42);
    double sum = 0.0, sum_sq = 0.0;

    if (cfg.layout == MemLayout::AOS) {
        for (int i = 0; i < cfg.num_simulations; ++i) {
            double t = run_one_simulation_aos(cfg, tyres, rng);
            sum    += t;
            sum_sq += t * t;
        }
    } else {
        SimStateSOA state;
        state.tyre_age.resize(cfg.num_simulations);
        state.fuel_load.resize(cfg.num_simulations);
        state.total_time.resize(cfg.num_simulations);
        state.current_lap.resize(cfg.num_simulations);
        state.compound_idx.resize(cfg.num_simulations);
        for (int i = 0; i < cfg.num_simulations; ++i) {
            double t = run_one_simulation_soa(cfg, tyres, rng, state, i);
            sum    += t;
            sum_sq += t * t;
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    double n = cfg.num_simulations;

    SimResult r;
    r.mean_race_time          = sum / n;
    r.stddev_race_time        = std::sqrt(sum_sq / n - (sum / n) * (sum / n));
    r.wall_clock_ms           = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    r.simulations_completed   = cfg.num_simulations;
    r.threads_used            = 1;
    r.layout                  = cfg.layout;
    r.impl_tag                = "single";
    return r;
}

} // namespace coa
} // namespace f1
