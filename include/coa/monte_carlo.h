#pragma once
#include "models/race_state.h"
#include "models/degradation.h"
#include <vector>
#include <string>

namespace f1 {
namespace coa {

// Memory layout options for COA study
enum class MemLayout { AOS, SOA };  // Array-of-Structs vs Struct-of-Arrays

struct SimConfig {
    int        num_simulations = 10000;
    int        num_threads     = 1;        // 1 = single-threaded
    MemLayout  layout          = MemLayout::AOS;
    bool       use_gpu         = false;
    RaceConfig race;
    // Noise parameters for Monte Carlo
    double     deg_noise_stddev    = 0.3;  // seconds
    double     sc_probability      = 0.1;  // safety car prob per lap
    double     rival_variation     = 0.5;  // seconds std-dev rival pace noise
};

struct SimResult {
    double mean_race_time;
    double stddev_race_time;
    double wall_clock_ms;
    long   simulations_completed;
    int    threads_used;
    MemLayout layout;
    std::string impl_tag;  // "single", "omp_N", "cuda"
};

// Single-threaded baseline
SimResult run_single(const SimConfig& cfg, const std::vector<TyreSpec>& tyres);

// OpenMP multi-threaded
SimResult run_omp(const SimConfig& cfg, const std::vector<TyreSpec>& tyres);

// CUDA GPU (compiled only if ENABLE_CUDA)
#ifdef ENABLE_CUDA
SimResult run_cuda(const SimConfig& cfg, const std::vector<TyreSpec>& tyres);
#endif

} // namespace coa
} // namespace f1
