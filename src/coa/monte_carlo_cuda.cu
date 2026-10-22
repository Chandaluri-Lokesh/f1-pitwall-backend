// CUDA Monte Carlo implementation — one GPU thread = one independent race simulation
// Compiled only when ENABLE_CUDA=ON (requires nvcc + CUDA toolkit)
//
// Architecture:
//   - Config and tyre data transferred H→D once at startup
//   - Each thread draws its own curand state (seed = global_thread_id, offset = 0)
//   - Kernel writes one double per thread to d_results[]
//   - Host reduces results to mean + stddev after D→H copy
//   - cudaEvent timing isolates H2D / kernel / D2H phases for COA analysis
//
// Lap-time model (matches CPU version in monte_carlo_single.cpp):
//   t_lap = base_lap_time
//         - fuel_effect_per_lap * lap_number          (fuel burn benefit)
//         + base_delta + deg_linear*age + deg_quad*age²  (tyre degradation)
//         + N(0, deg_noise_stddev)                    (stochastic noise)
//         + 15.0  if Bernoulli(sc_probability) fires  (safety car)
//
// Strategy (fixed — identical to OMP version):
//   Stint 1: compound 0 (soft),  laps 1 … N/2−1
//   Pit:     compound 1 (medium), lap  N/2  (+pit_loss_seconds)
//   Stint 2: compound 1 (medium), laps N/2+1 … N

#include "coa/monte_carlo.h"
#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace f1 {
namespace coa {

// ── Device-side tyre spec (only fields the kernel needs) ─────────────────────
struct CudaTyreSpec {
    double base_delta;
    double deg_linear;
    double deg_quadratic;
};

// ── Device-side configuration (plain-old-data, safe for cudaMemcpy / by-value) ─
struct CudaSimConfig {
    int    total_laps;
    int    num_simulations;
    int    pit_lap;
    int    pit_cmp;       // index into d_tyres[] for post-pit compound
    double pit_loss;
    double base_lap_time;
    double fuel_effect;   // seconds saved per lap as fuel burns off
    double deg_noise_std;
    double sc_prob;
};

// ── Device helpers ────────────────────────────────────────────────────────────

__device__ __forceinline__
double device_lap_time(const CudaTyreSpec* tyres,
                       int cmp, int age, int lap,
                       double base_lap_time, double fuel_effect) {
    double fuel_gain = fuel_effect * static_cast<double>(lap);
    double deg       = tyres[cmp].base_delta
                     + tyres[cmp].deg_linear    * static_cast<double>(age)
                     + tyres[cmp].deg_quadratic * static_cast<double>(age * age);
    return base_lap_time - fuel_gain + deg;
}

// ── Main kernel ───────────────────────────────────────────────────────────────
// Grid: ⌈num_simulations / THREADS⌉ blocks × THREADS threads/block
// Each thread simulates one complete race and writes its total race time to
// d_results[tid].  No inter-thread communication or shared memory required.

__global__
void mc_kernel(const CudaSimConfig cfg,
               const CudaTyreSpec* __restrict__ tyres,
               double*             __restrict__ d_results,
               unsigned long long               seed_base) {
    const int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= cfg.num_simulations) return;

    // Per-thread RNG — each thread gets a unique subsequence via the sequence
    // parameter so results are statistically independent across threads.
    curandState_t rng_state;
    curand_init(seed_base + static_cast<unsigned long long>(tid),
                /*sequence=*/0, /*offset=*/0, &rng_state);

    int    cmp      = 0;
    double tyre_age = 0.0;
    double total    = 0.0;

    for (int lap = 1; lap <= cfg.total_laps; ++lap) {
        // Safety car event: Bernoulli(sc_prob) → +15 s
        double sc_delta = (curand_uniform_double(&rng_state) < cfg.sc_prob)
                          ? 15.0 : 0.0;

        // Gaussian degradation noise: N(0, deg_noise_std)
        double noise = curand_normal_double(&rng_state) * cfg.deg_noise_std;

        if (lap == cfg.pit_lap && lap < cfg.total_laps) {
            // Pit stop lap: pay pit-lane time loss, fresh tyres of pit_cmp compound
            total   += cfg.pit_loss
                     + device_lap_time(tyres, cfg.pit_cmp, /*age=*/0, lap,
                                       cfg.base_lap_time, cfg.fuel_effect)
                     + noise + sc_delta;
            cmp      = cfg.pit_cmp;
            tyre_age = 1.0;
        } else {
            total   += device_lap_time(tyres, cmp, static_cast<int>(tyre_age), lap,
                                       cfg.base_lap_time, cfg.fuel_effect)
                     + noise + sc_delta;
            tyre_age += 1.0;
        }
    }

    d_results[tid] = total;
}

// ── Host-side reduction ───────────────────────────────────────────────────────
static void compute_stats(const double* results, int n,
                           double& mean, double& stddev) {
    double sum = 0.0, sum_sq = 0.0;
    for (int i = 0; i < n; ++i) {
        sum    += results[i];
        sum_sq += results[i] * results[i];
    }
    mean   = sum / static_cast<double>(n);
    stddev = std::sqrt(sum_sq / static_cast<double>(n) - mean * mean);
}

// ── RAII helper to avoid leaking CUDA resources on error ─────────────────────
struct CudaDeviceAlloc {
    void*  ptr  = nullptr;
    size_t size = 0;

    CudaDeviceAlloc() = default;
    explicit CudaDeviceAlloc(size_t bytes) : size(bytes) {
        cudaError_t e = cudaMalloc(&ptr, bytes);
        if (e != cudaSuccess)
            throw std::runtime_error(std::string("cudaMalloc failed: ")
                                     + cudaGetErrorString(e));
    }
    ~CudaDeviceAlloc() { if (ptr) cudaFree(ptr); }

    CudaDeviceAlloc(const CudaDeviceAlloc&)            = delete;
    CudaDeviceAlloc& operator=(const CudaDeviceAlloc&) = delete;

    template<typename T> T* as() { return static_cast<T*>(ptr); }
};

// ── Public API ────────────────────────────────────────────────────────────────
#ifdef ENABLE_CUDA
SimResult run_cuda(const SimConfig& cfg, const std::vector<TyreSpec>& tyres) {
    // ── Build device-side config ─────────────────────────────────────────────
    const int N       = cfg.race.total_laps;
    const int pit_lap = N / 2;
    const int pit_cmp = std::min(1, static_cast<int>(tyres.size()) - 1);

    CudaSimConfig dcfg;
    dcfg.total_laps      = N;
    dcfg.num_simulations = cfg.num_simulations;
    dcfg.pit_lap         = pit_lap;
    dcfg.pit_cmp         = pit_cmp;
    dcfg.pit_loss        = cfg.race.pit_loss_seconds;
    dcfg.base_lap_time   = cfg.race.base_lap_time;
    dcfg.fuel_effect     = cfg.race.fuel_effect_per_lap;
    dcfg.deg_noise_std   = cfg.deg_noise_stddev;
    dcfg.sc_prob         = cfg.sc_probability;

    // ── Pack tyre specs into flat array for GPU ──────────────────────────────
    std::vector<CudaTyreSpec> h_tyres(tyres.size());
    for (size_t i = 0; i < tyres.size(); ++i) {
        h_tyres[i].base_delta    = tyres[i].base_lap_time_delta;
        h_tyres[i].deg_linear    = tyres[i].deg_linear;
        h_tyres[i].deg_quadratic = tyres[i].deg_quadratic;
    }

    // ── Allocate device memory ───────────────────────────────────────────────
    CudaDeviceAlloc d_tyres  (h_tyres.size() * sizeof(CudaTyreSpec));
    CudaDeviceAlloc d_results(cfg.num_simulations * sizeof(double));

    // ── CUDA timing events ────────────────────────────────────────────────────
    // Measures H2D transfer, kernel execution, and D2H transfer separately
    // so the COA benchmark can report the breakdown.
    cudaEvent_t ev_h2d_s,  ev_h2d_e;
    cudaEvent_t ev_kern_s, ev_kern_e;
    cudaEvent_t ev_d2h_s,  ev_d2h_e;
    cudaEventCreate(&ev_h2d_s);  cudaEventCreate(&ev_h2d_e);
    cudaEventCreate(&ev_kern_s); cudaEventCreate(&ev_kern_e);
    cudaEventCreate(&ev_d2h_s);  cudaEventCreate(&ev_d2h_e);

    // ── H→D transfer ─────────────────────────────────────────────────────────
    cudaEventRecord(ev_h2d_s);
    cudaMemcpy(d_tyres.as<CudaTyreSpec>(), h_tyres.data(),
               h_tyres.size() * sizeof(CudaTyreSpec), cudaMemcpyHostToDevice);
    cudaEventRecord(ev_h2d_e);
    cudaEventSynchronize(ev_h2d_e);

    float ms_h2d = 0.0f;
    cudaEventElapsedTime(&ms_h2d, ev_h2d_s, ev_h2d_e);

    // ── Kernel launch ─────────────────────────────────────────────────────────
    // 256 threads/block balances occupancy vs register pressure for this kernel.
    // Each warp runs the same lap-loop body — no divergence except the pit-lap
    // branch, which fires once per race and is thus negligible.
    constexpr int THREADS = 256;
    const     int BLOCKS  = (cfg.num_simulations + THREADS - 1) / THREADS;

    cudaEventRecord(ev_kern_s);
    mc_kernel<<<BLOCKS, THREADS>>>(dcfg,
                                   d_tyres.as<CudaTyreSpec>(),
                                   d_results.as<double>(),
                                   /*seed_base=*/42ULL);
    cudaEventRecord(ev_kern_e);
    cudaEventSynchronize(ev_kern_e);

    cudaError_t kerr = cudaGetLastError();
    if (kerr != cudaSuccess)
        throw std::runtime_error(std::string("CUDA kernel error: ")
                                 + cudaGetErrorString(kerr));

    float ms_kernel = 0.0f;
    cudaEventElapsedTime(&ms_kernel, ev_kern_s, ev_kern_e);

    // ── D→H transfer ─────────────────────────────────────────────────────────
    std::vector<double> h_results(cfg.num_simulations);

    cudaEventRecord(ev_d2h_s);
    cudaMemcpy(h_results.data(), d_results.as<double>(),
               cfg.num_simulations * sizeof(double), cudaMemcpyDeviceToHost);
    cudaEventRecord(ev_d2h_e);
    cudaEventSynchronize(ev_d2h_e);

    float ms_d2h = 0.0f;
    cudaEventElapsedTime(&ms_d2h, ev_d2h_s, ev_d2h_e);

    // ── Destroy events ────────────────────────────────────────────────────────
    cudaEventDestroy(ev_h2d_s);  cudaEventDestroy(ev_h2d_e);
    cudaEventDestroy(ev_kern_s); cudaEventDestroy(ev_kern_e);
    cudaEventDestroy(ev_d2h_s);  cudaEventDestroy(ev_d2h_e);

    // ── Reduce on host ────────────────────────────────────────────────────────
    double mean = 0.0, stddev = 0.0;
    compute_stats(h_results.data(), cfg.num_simulations, mean, stddev);

    // ── Return result ─────────────────────────────────────────────────────────
    SimResult r;
    r.mean_race_time        = mean;
    r.stddev_race_time      = stddev;
    r.wall_clock_ms         = static_cast<double>(ms_h2d + ms_kernel + ms_d2h);
    r.simulations_completed = cfg.num_simulations;
    r.threads_used          = BLOCKS * THREADS;  // total GPU threads launched
    r.layout                = cfg.layout;
    r.impl_tag              = "cuda";
    return r;
}
#endif  // ENABLE_CUDA

} // namespace coa
} // namespace f1
