#include "httplib.h"
#include "nlohmann/json.hpp"
#include "dsa/dp_solver.h"
#include "dsa/greedy_solver.h"
#include "dsa/branch_bound_solver.h"
#include "dsa/graph_solver.h"
#include "dsa/genetic_solver.h"
#include "dsa/comparison_harness.h"
#include "models/degradation.h"
#include "models/driver.h"
#include "models/team.h"
#include "db/db_reader.h"
#include <algorithm>
#include <string>

using json = nlohmann::json;

namespace f1 {
namespace api {

static json strategy_result_to_json(const StrategyResult& r) {
    json j;
    j["algorithm"]          = r.algorithm;
    j["data_structure"]     = r.data_structure;
    j["driver_abbreviation"]= r.driver_abbreviation;
    j["team_id"]            = r.team_id;
    j["total_race_time"]    = r.total_race_time;
    j["stops_count"]        = r.stops_count;
    j["wall_clock_ms"]      = r.wall_clock_ms;
    j["states_explored"]    = r.states_explored;
    j["deviation_vs_actual"]= r.deviation_vs_actual;
    j["stops"] = json::array();
    for (const auto& stop : r.stops) {
        j["stops"].push_back({
            {"lap",      stop.lap},
            {"compound", compound_name(stop.new_compound)}
        });
    }
    return j;
}

static json driver_to_json(const Driver& d) {
    return {
        {"abbreviation",    d.abbreviation},
        {"full_name",       d.full_name},
        {"number",          d.number},
        {"team_id",         d.team_id},
        {"nationality",     d.nationality},
        {"tyre_mgmt_factor",d.tyre_mgmt_factor},
        {"qualifying_delta",d.qualifying_delta},
    };
}

static json team_to_json(const Team& t) {
    return {
        {"team_id",            t.team_id},
        {"team_name",          t.team_name},
        {"color",              t.color},
        {"pit_crew_time",      t.pit_crew_time},
        {"pit_crew_reliability",t.pit_crew_reliability},
    };
}

void register_dsa_routes(httplib::Server& svr, const std::string& data_dir) {

    // ------------------------------------------------------------------
    // GET /api/drivers  — all drivers (with tyre mgmt factors)
    // GET /api/drivers?team_id=ferrari
    // ------------------------------------------------------------------
    svr.Get("/api/drivers", [](const httplib::Request& req, httplib::Response& res) {
        try {
            std::vector<Driver> drivers;
            if (req.has_param("team_id")) {
                drivers = db::get_drivers_by_team(req.get_param_value("team_id"));
            } else {
                drivers = db::get_all_drivers();
            }
            json arr = json::array();
            for (const auto& d : drivers) arr.push_back(driver_to_json(d));
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // GET /api/teams  — all teams (with pit crew times)
    // ------------------------------------------------------------------
    svr.Get("/api/teams", [](const httplib::Request&, httplib::Response& res) {
        try {
            auto teams = db::get_all_teams();
            json arr = json::array();
            for (const auto& t : teams) arr.push_back(team_to_json(t));
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // GET /api/circuits  — all (circuit_id, year) pairs in DB
    // ------------------------------------------------------------------
    svr.Get("/api/dsa/circuits", [](const httplib::Request&, httplib::Response& res) {
        try {
            auto pairs = db::list_circuits();
            json arr = json::array();
            for (const auto& [cid, yr] : pairs) {
                arr.push_back({{"circuit_id", cid}, {"year", yr}});
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // GET /api/circuit?circuit_id=monza&year=2024
    // Returns full RaceConfig with pit_loss, base_lap_time, tyre_specs
    // ------------------------------------------------------------------
    svr.Get("/api/circuit", [](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string cid  = req.get_param_value("circuit_id");
            int         year = req.has_param("year") ? std::stoi(req.get_param_value("year")) : 2024;

            auto cfg_opt = db::get_circuit(cid, year);
            if (!cfg_opt) {
                res.status = 404;
                res.set_content(json({{"error", "circuit not found"}}).dump(), "application/json");
                return;
            }
            auto tyres = db::get_tyre_specs(cid, year);
            json tyre_arr = json::array();
            for (const auto& t : tyres) {
                tyre_arr.push_back({
                    {"compound",           compound_name(t.compound)},
                    {"base_lap_time_delta",t.base_lap_time_delta},
                    {"deg_linear",         t.deg_linear},
                    {"deg_quadratic",      t.deg_quadratic},
                    {"min_laps",           t.min_laps},
                    {"max_laps",           t.max_laps},
                });
            }
            const auto& c = *cfg_opt;
            json response = {
                {"circuit_id",          cid},
                {"year",                year},
                {"total_laps",          c.total_laps},
                {"pit_loss_seconds",    c.pit_loss_seconds},
                {"base_lap_time",       c.base_lap_time},
                {"fuel_effect_per_lap", c.fuel_effect_per_lap},
                {"tyre_specs",          tyre_arr},
            };
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // POST /api/dsa/solve
    // Body: {
    //   race: RaceConfig,
    //   algorithms: ["dp","greedy",...],
    //   driver: "LEC",         // optional
    //   team: "ferrari",       // optional
    //   data_structures: { "astar": "binary_heap", "dp": "3d_array" }  // optional
    // }
    // ------------------------------------------------------------------
    svr.Post("/api/dsa/solve", [data_dir](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);

            // Build RaceConfig
            RaceConfig config;
            config.total_laps         = body["race"].value("total_laps", 57);
            config.pit_loss_seconds   = body["race"].value("pit_loss_seconds", 21.5);
            config.base_lap_time      = body["race"].value("base_lap_time", 90.0);
            config.fuel_effect_per_lap= body["race"].value("fuel_effect_per_lap", 0.04);
            config.circuit_name       = body["race"].value("circuit_name", "generic");
            config.race_year          = body["race"].value("race_year", "2024");
            config.tyre_mgmt_factor   = 1.0;
            config.effective_pit_loss = config.pit_loss_seconds;

            // Apply driver context if provided
            std::string driver_abbr = body.value("driver", "");
            std::string team_id     = body.value("team",   "");

            if (!driver_abbr.empty()) {
                auto drv = db::get_driver(driver_abbr);
                if (!drv) drv = default_driver_params(driver_abbr);
                config.driver_abbreviation = drv->abbreviation;
                config.tyre_mgmt_factor    = drv->tyre_mgmt_factor;
            }
            if (!team_id.empty()) {
                auto tm = db::get_team(team_id);
                Team team_data = tm ? *tm : default_team_params(team_id);
                config.team_id            = team_data.team_id;
                config.effective_pit_loss = effective_pit_loss(config.pit_loss_seconds, team_data);
            }

            // Load tyre specs from DB (real fitted curves if available)
            auto tyres = db::get_tyre_specs(config.circuit_name,
                                            config.race_year.empty() ? 2024
                                            : std::stoi(config.race_year));

            // Sanitize: remove INTERMEDIATE/WET and compounds where max_laps <= 1
            // (max_laps == 1 is the pipeline's sentinel for a failed/unreliable curve fit)
            tyres.erase(
                std::remove_if(tyres.begin(), tyres.end(), [](const TyreSpec& t) {
                    return t.compound == Compound::INTERMEDIATE
                        || t.compound == Compound::WET
                        || t.max_laps <= 1;
                }),
                tyres.end()
            );
            // If fewer than 2 usable compounds remain, fall back to defaults
            if (tyres.size() < 2) tyres = default_tyre_specs();

            // Apply tyre management factor to deg_linear of all compounds
            if (config.tyre_mgmt_factor != 1.0) {
                for (auto& t : tyres) {
                    t.deg_linear    *= config.tyre_mgmt_factor;
                    t.deg_quadratic *= config.tyre_mgmt_factor;
                }
            }

            // Optional per-algorithm data structure selection
            json ds_map = body.value("data_structures", json::object());

            // Build harness
            dsa::ComparisonHarness harness;
            auto algos = body.value("algorithms",
                         std::vector<std::string>{"dp","greedy","bb","astar","ga"});
            for (const auto& a : algos) {
                if (a == "dp")       harness.add_solver(std::make_unique<dsa::DPSolver>());
                if (a == "greedy")   harness.add_solver(std::make_unique<dsa::GreedySolver>());
                if (a == "bb")       harness.add_solver(std::make_unique<dsa::BranchBoundSolver>());
                if (a == "dijkstra") harness.add_solver(std::make_unique<dsa::GraphSolver>(dsa::GraphAlgo::DIJKSTRA));
                if (a == "astar")    harness.add_solver(std::make_unique<dsa::GraphSolver>(dsa::GraphAlgo::ASTAR));
                if (a == "ga")       harness.add_solver(std::make_unique<dsa::GeneticSolver>());
            }

            auto hr = harness.run(config, tyres);

            // Tag each result with driver/team and data structure used
            for (auto& r : hr.results) {
                r.driver_abbreviation = config.driver_abbreviation;
                r.team_id             = config.team_id;
                if (ds_map.contains(r.algorithm))
                    r.data_structure = ds_map[r.algorithm].get<std::string>();
            }
            hr.best.driver_abbreviation = config.driver_abbreviation;
            hr.best.team_id             = config.team_id;

            // Persist to DB
            for (const auto& r : hr.results) db::save_strategy_run(config, r);

            json response;
            response["best"]    = strategy_result_to_json(hr.best);
            response["results"] = json::array();
            for (const auto& r : hr.results)
                response["results"].push_back(strategy_result_to_json(r));
            response["effective_pit_loss"]   = config.effective_pit_loss;
            response["tyre_mgmt_factor"]     = config.tyre_mgmt_factor;
            response["driver_abbreviation"]  = config.driver_abbreviation;
            response["team_id"]              = config.team_id;

            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });
}

} // namespace api
} // namespace f1
