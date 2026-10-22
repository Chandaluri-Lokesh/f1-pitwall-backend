#include "db/db_reader.h"
#include "models/degradation.h"
#include <sqlite3.h>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <ctime>

namespace f1 {
namespace db {

static sqlite3* g_db = nullptr;

void open(const std::string& db_path) {
    if (g_db) return;
    int rc = sqlite3_open_v2(db_path.c_str(), &g_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Cannot open DB: " + db_path +
                                 " — " + sqlite3_errmsg(g_db));
    }
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;",
                 nullptr, nullptr, nullptr);
    std::cout << "[DB] Opened " << db_path << "\n";
}

void close() {
    if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
}

// ---------------------------------------------------------------------------
// Circuits
// ---------------------------------------------------------------------------

std::optional<RaceConfig> get_circuit(const std::string& circuit_id, int year) {
    if (!g_db) return std::nullopt;

    const char* sql =
        "SELECT total_laps, pit_loss_seconds, base_lap_time, fuel_effect_per_lap, gp_name "
        "FROM circuits WHERE circuit_id=? AND year=? LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, circuit_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, year);

    std::optional<RaceConfig> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        RaceConfig cfg;
        cfg.total_laps           = sqlite3_column_int   (stmt, 0);
        cfg.pit_loss_seconds     = sqlite3_column_double(stmt, 1);
        cfg.base_lap_time        = sqlite3_column_double(stmt, 2);
        cfg.fuel_effect_per_lap  = sqlite3_column_double(stmt, 3);
        cfg.circuit_name         = circuit_id;
        cfg.race_year            = std::to_string(year);
        cfg.track_temp_celsius   = 30.0;   // default — not stored yet
        cfg.tyre_mgmt_factor     = 1.0;
        cfg.effective_pit_loss   = cfg.pit_loss_seconds;
        result = cfg;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::pair<std::string, int>> list_circuits() {
    std::vector<std::pair<std::string, int>> out;
    if (!g_db) return out;
    const char* sql = "SELECT circuit_id, year FROM circuits ORDER BY year DESC, circuit_id";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string cid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int yr          = sqlite3_column_int(stmt, 1);
        out.emplace_back(cid, yr);
    }
    sqlite3_finalize(stmt);
    return out;
}

// ---------------------------------------------------------------------------
// Tyre specs
// ---------------------------------------------------------------------------

std::vector<TyreSpec> get_tyre_specs(const std::string& circuit_id, int year) {
    if (!g_db) return default_tyre_specs();

    const char* sql =
        "SELECT compound, base_delta, deg_linear, deg_quadratic, min_laps, max_laps "
        "FROM tyre_specs WHERE circuit_id=? AND year=?";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, circuit_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, year);

    std::vector<TyreSpec> specs;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TyreSpec s;
        std::string cname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        try { s.compound = compound_from_string(cname); }
        catch (...) { continue; }
        s.base_lap_time_delta = sqlite3_column_double(stmt, 1);
        s.deg_linear          = sqlite3_column_double(stmt, 2);
        s.deg_quadratic       = sqlite3_column_double(stmt, 3);
        s.min_laps            = sqlite3_column_int   (stmt, 4);
        s.max_laps            = sqlite3_column_int   (stmt, 5);
        specs.push_back(s);
    }
    sqlite3_finalize(stmt);
    return specs.empty() ? default_tyre_specs() : specs;
}

// ---------------------------------------------------------------------------
// Drivers
// ---------------------------------------------------------------------------

static Driver row_to_driver(sqlite3_stmt* stmt) {
    Driver d;
    auto col = [&](int i) -> std::string {
        auto* t = sqlite3_column_text(stmt, i);
        return t ? reinterpret_cast<const char*>(t) : "";
    };
    d.abbreviation = col(0);
    d.full_name    = col(1);
    d.number       = sqlite3_column_int(stmt, 2);
    d.team_id      = col(3);
    d.nationality  = col(4);

    // Merge in known params (tyre_mgmt_factor etc.)
    Driver defaults = default_driver_params(d.abbreviation);
    d.tyre_mgmt_factor  = defaults.tyre_mgmt_factor;
    d.qualifying_delta  = defaults.qualifying_delta;
    return d;
}

std::vector<Driver> get_all_drivers() {
    std::vector<Driver> out;
    if (!g_db) return out;
    const char* sql =
        "SELECT abbreviation, full_name, number, team_id, nationality "
        "FROM drivers ORDER BY full_name";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(row_to_driver(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::vector<Driver> get_drivers_by_team(const std::string& team_id) {
    std::vector<Driver> out;
    if (!g_db) return out;
    const char* sql =
        "SELECT abbreviation, full_name, number, team_id, nationality "
        "FROM drivers WHERE team_id=? ORDER BY full_name";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, team_id.c_str(), -1, SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(row_to_driver(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::optional<Driver> get_driver(const std::string& abbreviation) {
    if (!g_db) return std::nullopt;
    const char* sql =
        "SELECT abbreviation, full_name, number, team_id, nationality "
        "FROM drivers WHERE abbreviation=? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, abbreviation.c_str(), -1, SQLITE_STATIC);
    std::optional<Driver> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = row_to_driver(stmt);
    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------------------
// Teams
// ---------------------------------------------------------------------------

static Team row_to_team(sqlite3_stmt* stmt) {
    Team t;
    auto col = [&](int i) -> std::string {
        auto* txt = sqlite3_column_text(stmt, i);
        return txt ? reinterpret_cast<const char*>(txt) : "";
    };
    t.team_id   = col(0);
    t.team_name = col(1);
    t.color     = col(2);
    Team defaults = default_team_params(t.team_id);
    t.pit_crew_time        = defaults.pit_crew_time;
    t.pit_crew_reliability = defaults.pit_crew_reliability;
    return t;
}

std::vector<Team> get_all_teams() {
    std::vector<Team> out;
    if (!g_db) return out;
    const char* sql = "SELECT team_id, team_name, color FROM teams ORDER BY team_name";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(row_to_team(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::optional<Team> get_team(const std::string& team_id) {
    if (!g_db) return std::nullopt;
    const char* sql = "SELECT team_id, team_name, color FROM teams WHERE team_id=? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, team_id.c_str(), -1, SQLITE_STATIC);
    std::optional<Team> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = row_to_team(stmt);
    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------------------
// Saving run results
// ---------------------------------------------------------------------------

void save_strategy_run(const RaceConfig& config, const StrategyResult& result) {
    if (!g_db) return;
    // Build stops JSON string
    std::string stops_json = "[";
    for (size_t i = 0; i < result.stops.size(); ++i) {
        if (i) stops_json += ",";
        stops_json += "{\"lap\":" + std::to_string(result.stops[i].lap) +
                      ",\"compound\":\"" + compound_name(result.stops[i].new_compound) + "\"}";
    }
    stops_json += "]";

    // ISO timestamp
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char ts[32]; std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", std::gmtime(&t));

    const char* sql =
        "INSERT INTO strategy_runs "
        "(created_at,circuit_id,year,algorithm,data_structure,total_race_time,"
        " stops_count,wall_clock_ms,states_explored,stops_json) "
        "VALUES (?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1,  ts,                                          -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2,  config.circuit_name.c_str(),                 -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 3,  config.race_year.empty() ? 0 : std::stoi(config.race_year));
    sqlite3_bind_text(stmt, 4,  result.algorithm.c_str(),                    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5,  result.data_structure.c_str(),               -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt,6, result.total_race_time);
    sqlite3_bind_int (stmt, 7,  result.stops_count);
    sqlite3_bind_double(stmt,8, result.wall_clock_ms);
    sqlite3_bind_int64(stmt,9,  result.states_explored);
    sqlite3_bind_text(stmt, 10, stops_json.c_str(),                          -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void save_benchmark_run(const std::string& circuit_id, int year,
                        const std::string& impl_tag, int num_simulations,
                        int num_threads, const std::string& layout,
                        const std::string& opt_level,
                        double mean_time, double stddev_time, double wall_ms) {
    if (!g_db) return;
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char ts[32]; std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", std::gmtime(&t));

    const char* sql =
        "INSERT INTO benchmark_runs "
        "(created_at,circuit_id,year,impl_tag,num_simulations,num_threads,"
        " layout,opt_level,mean_race_time,stddev_race_time,wall_clock_ms) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text  (stmt, 1,  ts,                  -1, SQLITE_STATIC);
    sqlite3_bind_text  (stmt, 2,  circuit_id.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_int   (stmt, 3,  year);
    sqlite3_bind_text  (stmt, 4,  impl_tag.c_str(),    -1, SQLITE_STATIC);
    sqlite3_bind_int   (stmt, 5,  num_simulations);
    sqlite3_bind_int   (stmt, 6,  num_threads);
    sqlite3_bind_text  (stmt, 7,  layout.c_str(),      -1, SQLITE_STATIC);
    sqlite3_bind_text  (stmt, 8,  opt_level.c_str(),   -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 9,  mean_time);
    sqlite3_bind_double(stmt, 10, stddev_time);
    sqlite3_bind_double(stmt, 11, wall_ms);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

} // namespace db
} // namespace f1
