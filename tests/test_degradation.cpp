#include "models/degradation.h"
#include "models/tyre.h"
#include <stdexcept>
#include <functional>
#include <string>

// Forward declarations from test_main.cpp
void register_test(const std::string& name, std::function<void()> fn);
#define ASSERT_NEAR(a,b,eps) do { double _d=(a)-(b); if(_d<0)_d=-_d; if(_d>(eps)) throw std::runtime_error("assert near failed"); } while(0)
#define ASSERT_TRUE(x) do { if(!(x)) throw std::runtime_error("assert true failed"); } while(0)

static int _reg = []() -> int {

    register_test("tyre_lap_time_delta_fresh_soft_is_negative", []() {
        f1::TyreSpec soft{ f1::Compound::SOFT, -0.5, 0.08, 0.003, 5, 30 };
        double delta = f1::tyre_lap_time_delta(soft, 0);
        ASSERT_NEAR(delta, -0.5, 1e-9);
    });

    register_test("tyre_lap_time_delta_increases_with_age", []() {
        f1::TyreSpec med{ f1::Compound::MEDIUM, 0.0, 0.05, 0.002, 10, 40 };
        double d0 = f1::tyre_lap_time_delta(med, 0);
        double d10 = f1::tyre_lap_time_delta(med, 10);
        ASSERT_TRUE(d10 > d0);
    });

    register_test("lap_time_decreases_with_fuel_burn", []() {
        f1::RaceConfig cfg;
        cfg.total_laps = 57; cfg.pit_loss_seconds = 21.5;
        cfg.base_lap_time = 90.0; cfg.fuel_effect_per_lap = 0.04;
        f1::TyreSpec hard{ f1::Compound::HARD, 0.4, 0.03, 0.001, 15, 55 };
        double t_lap1  = f1::lap_time(cfg, hard, 0, 1);
        double t_lap50 = f1::lap_time(cfg, hard, 0, 50);
        ASSERT_TRUE(t_lap50 < t_lap1);
    });

    register_test("default_tyre_specs_has_three_compounds", []() {
        auto specs = f1::default_tyre_specs();
        if (specs.size() != 3) throw std::runtime_error("expected 3 specs");
    });

    return 0;
}();
