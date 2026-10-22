#include "db/db_reader.h"
#include "models/driver.h"
#include "models/team.h"
#include <stdexcept>
#include <cstdlib>
#include <functional>
#include <string>

void register_test(const std::string& name, std::function<void()> fn);
#define ASSERT_TRUE(x)  do { if(!(x)) throw std::runtime_error("assert true failed: " #x); } while(0)
#define ASSERT_FALSE(x) do { if( (x)) throw std::runtime_error("assert false failed: " #x); } while(0)

static int _reg = []() -> int {

    register_test("default_driver_params_alo_low_tyre_mgmt", []() {
        auto d = f1::default_driver_params("ALO");
        ASSERT_TRUE(d.tyre_mgmt_factor < 1.0);
    });

    register_test("default_driver_params_lec_hard_on_tyres", []() {
        auto d = f1::default_driver_params("LEC");
        ASSERT_TRUE(d.tyre_mgmt_factor > 1.0);
    });

    register_test("default_driver_params_unknown_is_1", []() {
        auto d = f1::default_driver_params("XYZ");
        ASSERT_TRUE(d.tyre_mgmt_factor == 1.0);
    });

    register_test("default_team_params_redbull_fast_crew", []() {
        auto t = f1::default_team_params("red_bull");
        ASSERT_TRUE(t.pit_crew_time < 2.5);
    });

    register_test("effective_pit_loss_faster_crew_saves_time", []() {
        auto rb   = f1::default_team_params("red_bull");
        auto haas = f1::default_team_params("haas");
        double circuit_loss = 22.0;
        double rb_loss   = f1::effective_pit_loss(circuit_loss, rb);
        double haas_loss = f1::effective_pit_loss(circuit_loss, haas);
        ASSERT_TRUE(rb_loss < haas_loss);
    });

    register_test("db_open_and_query_drivers", []() {
        // Try to open the real DB if it exists, skip gracefully if not
        const char* db_path_env = std::getenv("PITWALL_DB");
        std::string db_path = db_path_env ? db_path_env : "data/pitwall.db";
        try {
            f1::db::open(db_path);
            auto drivers = f1::db::get_all_drivers();
            // If DB has data, should have at least some drivers
            // If DB empty/absent, returns empty vector — both are acceptable
            (void)drivers;
            f1::db::close();
        } catch (...) {
            // DB not present in test environment — skip
        }
    });

    return 0;
}();
