#include "coa/monte_carlo.h"
#include "coa/profiler.h"
#include "models/degradation.h"
#include <iostream>
#include <string>

// COA Benchmark entry point — compiled separately for each -O level
// Usage: ./bench_O3 [num_simulations] [num_threads] [aos|soa]

int main(int argc, char* argv[]) {
    int num_sims    = argc >= 2 ? std::stoi(argv[1]) : 10000;
    int num_threads = argc >= 3 ? std::stoi(argv[2]) : 1;
    std::string layout_str = argc >= 4 ? argv[3] : "aos";

    f1::coa::SimConfig cfg;
    cfg.num_simulations = num_sims;
    cfg.num_threads     = num_threads;
    cfg.layout          = (layout_str == "soa")
                          ? f1::coa::MemLayout::SOA
                          : f1::coa::MemLayout::AOS;

    cfg.race.total_laps       = 57;
    cfg.race.pit_loss_seconds = 21.5;
    cfg.race.base_lap_time    = 90.0;
    cfg.race.fuel_effect_per_lap = 0.04;
    cfg.race.circuit_name     = "monza";

    auto tyres = f1::default_tyre_specs();
    f1::coa::Profiler profiler;

    std::cout << "[COA Bench] sims=" << num_sims
              << " threads=" << num_threads
              << " layout=" << layout_str << "\n";

    // Single-threaded
    profiler.start("single-" + layout_str);
    auto r_single = f1::coa::run_single(cfg, tyres);
    profiler.stop();
    std::cout << "Single: " << r_single.wall_clock_ms << " ms\n";

    // Multi-threaded
    if (num_threads > 1) {
        profiler.start("omp-" + std::to_string(num_threads) + "-" + layout_str);
        auto r_omp = f1::coa::run_omp(cfg, tyres);
        profiler.stop();
        std::cout << "OMP x" << num_threads << ": "
                  << r_omp.wall_clock_ms << " ms  "
                  << "speedup=" << r_single.wall_clock_ms / r_omp.wall_clock_ms << "x\n";
    }

    profiler.print_summary();
    return 0;
}
