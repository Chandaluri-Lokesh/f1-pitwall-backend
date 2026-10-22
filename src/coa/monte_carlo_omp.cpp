#include "coa/monte_carlo.h"
#include "models/degradation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

// Monte Carlo Race Simulation — OpenMP multi-threaded
// =====================================================
// Same stochastic model as the single-threaded version:
//   - Gaussian degradation noise per lap
//   - Bernoulli safety car events (+15s)
//   - Fixed strategy: soft start, one stop at lap N/2 to medium
//
// OMP design:
//   - One std::mt19937 per thread, seeded with (42 + thread_id)
//     → No shared RNG state, no false sharing, no locks
//   - reduction(+:sum, sum_sq) collapses partial sums safely
//   - schedule(dynamic, 64) balances load if SC events vary runtime
//   - Demonstrates Amdahl's Law: the serial fraction is setup/teardown only

namespace f1 {
namespace coa {

SimResult run_omp(const SimConfig& cfg, const std::vector<TyreSpec>& tyres) {
    auto t_start = std::chrono::steady_clock::now();

    double sum = 0.0, sum_sq = 0.0;

    const int    N        = cfg.race.total_laps;
    const double pit_loss = cfg.race.pit_loss_seconds;
    const int    pit_lap  = N / 2;
    const int    pit_cmp  = std::min(1, static_cast<int>(tyres.size()) - 1);

#ifdef _OPENMP
    omp_set_num_threads(cfg.num_threads);

    // Each thread gets its own RNG — no sharing, no contention.
    // The parallel region is opened once; the for loop distributes iterations.
    #pragma omp parallel reduction(+:sum, sum_sq)
    {
        const int tid = omp_get_thread_num();
        std::mt19937 rng(42u + static_cast<unsigned>(tid));
        std::normal_distribution<double> deg_noise(0.0, cfg.deg_noise_stddev);
        std::bernoulli_distribution      sc_event(cfg.sc_probability);

        #pragma omp for schedule(dynamic, 64)
        for (int i = 0; i < cfg.num_simulations; ++i) {
            double total = 0.0;
            int    cmp   = 0;
            double age   = 0.0;

            for (int lap = 1; lap <= N; ++lap) {
                double sc_delta = sc_event(rng) ? 15.0 : 0.0;

                if (lap == pit_lap && lap < N) {
                    total += pit_loss
                           + lap_time(cfg.race, tyres[pit_cmp], 0, lap)
                           + deg_noise(rng) + sc_delta;
                    cmp   = pit_cmp;
                    age   = 1.0;
                } else {
                    total += lap_time(cfg.race, tyres[cmp],
                                      static_cast<int>(age), lap)
                           + deg_noise(rng) + sc_delta;
                    age   += 1.0;
                }
            }
            sum    += total;
            sum_sq += total * total;
        }
    }  // end parallel
#else
    // Serial fallback when OpenMP is unavailable
    std::mt19937 rng(42u);
    std::normal_distribution<double> deg_noise(0.0, cfg.deg_noise_stddev);
    std::bernoulli_distribution      sc_event(cfg.sc_probability);

    for (int i = 0; i < cfg.num_simulations; ++i) {
        double total = 0.0;
        int    cmp   = 0;
        double age   = 0.0;

        for (int lap = 1; lap <= N; ++lap) {
            double sc_delta = sc_event(rng) ? 15.0 : 0.0;

            if (lap == pit_lap && lap < N) {
                total += pit_loss
                       + lap_time(cfg.race, tyres[pit_cmp], 0, lap)
                       + deg_noise(rng) + sc_delta;
                cmp   = pit_cmp;
                age   = 1.0;
            } else {
                total += lap_time(cfg.race, tyres[cmp],
                                  static_cast<int>(age), lap)
                       + deg_noise(rng) + sc_delta;
                age   += 1.0;
            }
        }
        sum    += total;
        sum_sq += total * total;
    }
#endif

    auto t_end = std::chrono::steady_clock::now();
    const double n = static_cast<double>(cfg.num_simulations);

    SimResult r;
    r.mean_race_time        = n > 0.0 ? sum / n : 0.0;
    r.stddev_race_time      = n > 0.0
                            ? std::sqrt(std::max(0.0, sum_sq / n - (sum / n) * (sum / n)))
                            : 0.0;
    r.wall_clock_ms         = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    r.simulations_completed = cfg.num_simulations;
#ifdef _OPENMP
    r.threads_used          = omp_get_max_threads();
#else
    r.threads_used          = 1;
#endif
    r.layout                = cfg.layout;
    r.impl_tag              = "omp_" + std::to_string(r.threads_used);
    return r;
}

} // namespace coa
} // namespace f1
