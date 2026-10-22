#include "dsa/dp_solver.h"
#include "dsa/greedy_solver.h"
#include "dsa/graph_solver.h"
#include "dsa/branch_bound_solver.h"
#include "dsa/comparison_harness.h"
#include "models/degradation.h"
#include <stdexcept>
#include <sstream>
#include <functional>
#include <string>
#include <cmath>

void register_test(const std::string& name, std::function<void()> fn);
#define ASSERT_TRUE(x) do { if(!(x)) throw std::runtime_error("assert true failed: " #x); } while(0)

static f1::RaceConfig make_short_race() {
    f1::RaceConfig cfg;
    cfg.total_laps          = 20;
    cfg.pit_loss_seconds    = 21.5;
    cfg.base_lap_time       = 90.0;
    cfg.fuel_effect_per_lap = 0.04;
    cfg.circuit_name        = "test";
    return cfg;
}

static int _reg = []() -> int {

    register_test("harness_runs_all_solvers", []() {
        auto cfg   = make_short_race();
        auto tyres = f1::default_tyre_specs();

        f1::dsa::ComparisonHarness harness;
        harness.add_solver(std::make_unique<f1::dsa::DPSolver>());
        harness.add_solver(std::make_unique<f1::dsa::GreedySolver>());
        harness.add_solver(std::make_unique<f1::dsa::GraphSolver>());

        auto hr = harness.run(cfg, tyres);
        ASSERT_TRUE(hr.results.size() == 3);
    });

    register_test("exact_solvers_agree_on_optimum", []() {
        // DP, A*, and B&B are all exact — they must produce the same total_race_time
        auto cfg   = make_short_race();
        auto tyres = f1::default_tyre_specs();

        f1::dsa::DPSolver         dp;
        f1::dsa::GraphSolver      astar(f1::dsa::GraphAlgo::ASTAR);
        f1::dsa::GraphSolver      dijkstra(f1::dsa::GraphAlgo::DIJKSTRA);
        f1::dsa::BranchBoundSolver bb;
        f1::dsa::GreedySolver     greedy;

        auto r_dp   = dp.solve(cfg, tyres);
        auto r_as   = astar.solve(cfg, tyres);
        auto r_dij  = dijkstra.solve(cfg, tyres);
        auto r_bb   = bb.solve(cfg, tyres);
        auto r_gr   = greedy.solve(cfg, tyres);

        const double EPS = 1e-4;
        ASSERT_TRUE(std::fabs(r_dp.total_race_time - r_as.total_race_time)  < EPS);
        ASSERT_TRUE(std::fabs(r_dp.total_race_time - r_dij.total_race_time) < EPS);
        ASSERT_TRUE(std::fabs(r_dp.total_race_time - r_bb.total_race_time)  < EPS);
        // Greedy must be >= exact (never better than optimum)
        ASSERT_TRUE(r_gr.total_race_time >= r_dp.total_race_time - EPS);
    });

    register_test("harness_print_table_no_crash", []() {
        auto cfg   = make_short_race();
        auto tyres = f1::default_tyre_specs();

        f1::dsa::ComparisonHarness harness;
        harness.add_solver(std::make_unique<f1::dsa::GreedySolver>());

        auto hr = harness.run(cfg, tyres);
        std::ostringstream oss;
        f1::dsa::ComparisonHarness::print_table(hr, oss);
        ASSERT_TRUE(!oss.str().empty());
    });

    return 0;
}();
