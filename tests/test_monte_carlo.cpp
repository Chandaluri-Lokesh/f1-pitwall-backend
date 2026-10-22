#include "coa/monte_carlo.h"
#include "models/degradation.h"
#include <stdexcept>
#include <functional>
#include <string>

void register_test(const std::string& name, std::function<void()> fn);
#define ASSERT_TRUE(x) do { if(!(x)) throw std::runtime_error("assert true failed: " #x); } while(0)

static f1::coa::SimConfig make_small_cfg() {
    f1::coa::SimConfig cfg;
    cfg.num_simulations         = 100;
    cfg.num_threads             = 1;
    cfg.layout                  = f1::coa::MemLayout::AOS;
    cfg.race.total_laps         = 20;
    cfg.race.pit_loss_seconds   = 21.5;
    cfg.race.base_lap_time      = 90.0;
    cfg.race.fuel_effect_per_lap= 0.04;
    return cfg;
}

static int _reg = []() -> int {

    register_test("single_sim_completes", []() {
        auto cfg   = make_small_cfg();
        auto tyres = f1::default_tyre_specs();
        auto r     = f1::coa::run_single(cfg, tyres);
        ASSERT_TRUE(r.simulations_completed == 100);
        ASSERT_TRUE(r.wall_clock_ms >= 0.0);
    });

    register_test("omp_sim_completes", []() {
        auto cfg        = make_small_cfg();
        cfg.num_threads = 2;
        auto tyres = f1::default_tyre_specs();
        auto r     = f1::coa::run_omp(cfg, tyres);
        ASSERT_TRUE(r.simulations_completed == 100);
    });

    return 0;
}();
