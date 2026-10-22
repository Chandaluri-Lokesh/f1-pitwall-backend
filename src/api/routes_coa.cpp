#include "httplib.h"
#include "nlohmann/json.hpp"
#include "coa/monte_carlo.h"
#include "models/degradation.h"

using json = nlohmann::json;

namespace f1 {
namespace api {

// POST /api/coa/benchmark
// Body: { num_simulations, num_threads, layout: "aos"|"soa", race: RaceConfig }
// Returns: SimResult as JSON

static json sim_result_to_json(const coa::SimResult& r) {
    return {
        {"impl_tag",             r.impl_tag},
        {"mean_race_time",       r.mean_race_time},
        {"stddev_race_time",     r.stddev_race_time},
        {"wall_clock_ms",        r.wall_clock_ms},
        {"simulations_completed",r.simulations_completed},
        {"threads_used",         r.threads_used},
        {"layout",               r.layout == coa::MemLayout::AOS ? "aos" : "soa"}
    };
}

void register_coa_routes(httplib::Server& svr) {

    // POST /api/coa/benchmark — run simulation and return timing results
    svr.Post("/api/coa/benchmark", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);

            coa::SimConfig cfg;
            cfg.num_simulations = body.value("num_simulations", 10000);
            cfg.num_threads     = body.value("num_threads", 1);
            cfg.layout          = body.value("layout", "aos") == "soa"
                                  ? coa::MemLayout::SOA : coa::MemLayout::AOS;

            cfg.race.total_laps         = body["race"].value("total_laps", 57);
            cfg.race.pit_loss_seconds   = body["race"].value("pit_loss_seconds", 21.5);
            cfg.race.base_lap_time      = body["race"].value("base_lap_time", 90.0);
            cfg.race.fuel_effect_per_lap= body["race"].value("fuel_effect_per_lap", 0.04);
            cfg.race.circuit_name       = body["race"].value("circuit_name", "generic");

            auto tyres = load_tyre_specs(cfg.race.circuit_name);

            json results = json::array();

            // Single-threaded run
            cfg.num_threads = 1;
            results.push_back(sim_result_to_json(coa::run_single(cfg, tyres)));

            // OMP runs (1, 2, 4, 8 threads if requested)
            if (body.value("num_threads", 1) > 1) {
                for (int t : {2, 4, 8}) {
                    if (t > body.value("num_threads", 1)) break;
                    cfg.num_threads = t;
                    results.push_back(sim_result_to_json(coa::run_omp(cfg, tyres)));
                }
            }

            // CUDA run (only when compiled with ENABLE_CUDA and client requests it)
#ifdef ENABLE_CUDA
            if (body.value("use_gpu", false)) {
                cfg.use_gpu = true;
                results.push_back(sim_result_to_json(coa::run_cuda(cfg, tyres)));
            }
#endif

            res.set_content(json({{"results", results}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });
}

} // namespace api
} // namespace f1
