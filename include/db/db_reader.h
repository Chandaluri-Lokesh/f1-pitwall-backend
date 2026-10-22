#pragma once
#include "models/driver.h"
#include "models/team.h"
#include "models/race_state.h"
#include "models/degradation.h"
#include <vector>
#include <string>
#include <optional>

namespace f1 {
namespace db {

// Opens (or re-uses) the SQLite connection to pitwall.db
// db_path: absolute path to pitwall.db
void open(const std::string& db_path);
void close();

// ---- Circuits -----------------------------------------------------------
// Returns a RaceConfig populated from the circuits table.
// Returns nullopt if circuit/year not found.
std::optional<RaceConfig> get_circuit(const std::string& circuit_id, int year);

// List all (circuit_id, year) pairs available in the DB
std::vector<std::pair<std::string, int>> list_circuits();

// ---- Tyre specs ---------------------------------------------------------
// Returns fitted TyreSpecs for a circuit/year from the DB.
// Falls back to default_tyre_specs() if none found.
std::vector<TyreSpec> get_tyre_specs(const std::string& circuit_id, int year);

// ---- Drivers ------------------------------------------------------------
std::vector<Driver> get_all_drivers();
std::vector<Driver> get_drivers_by_team(const std::string& team_id);
std::optional<Driver> get_driver(const std::string& abbreviation);

// ---- Teams --------------------------------------------------------------
std::vector<Team> get_all_teams();
std::optional<Team> get_team(const std::string& team_id);

// ---- Saving run results -------------------------------------------------
void save_strategy_run(const RaceConfig& config, const StrategyResult& result);
void save_benchmark_run(const std::string& circuit_id, int year,
                        const std::string& impl_tag, int num_simulations,
                        int num_threads, const std::string& layout,
                        const std::string& opt_level,
                        double mean_time, double stddev_time, double wall_ms);

} // namespace db
} // namespace f1
