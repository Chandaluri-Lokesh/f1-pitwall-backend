#pragma once
#include "tyre.h"
#include <vector>
#include <string>

namespace f1 {

struct RaceConfig {
    int         total_laps           = 57;
    double      pit_loss_seconds     = 21.5;   // base circuit pit lane time loss
    double      base_lap_time        = 90.0;   // reference fastest lap (seconds)
    double      fuel_effect_per_lap  = 0.04;   // seconds gained per lap as fuel burns off
    double      track_temp_celsius   = 30.0;
    std::string circuit_name;
    std::string race_year;

    // Driver / Team context (optional — defaults applied if empty)
    std::string driver_abbreviation;    // e.g. "LEC" — affects tyre_mgmt_factor
    std::string team_id;                // e.g. "ferrari" — affects effective pit loss
    double      tyre_mgmt_factor     = 1.0;  // 1.0 = neutral; < 1.0 = driver extends tyres
    double      effective_pit_loss   = 21.5; // circuit_pit_loss adjusted for team crew speed
};

// A single DSA state node: (lap, compound, tyre_age)
struct StrategyState {
    int      lap;
    Compound compound;
    int      tyre_age;

    bool operator==(const StrategyState& o) const {
        return lap == o.lap && compound == o.compound && tyre_age == o.tyre_age;
    }
};

struct PitStop {
    int      lap;
    Compound new_compound;
};

struct StrategyResult {
    std::vector<PitStop> stops;
    double               total_race_time    = 0.0;   // seconds
    int                  stops_count        = 0;
    double               wall_clock_ms      = 0.0;   // solver wall-clock time
    long long            states_explored    = 0;     // nodes visited
    std::string          algorithm;
    std::string          data_structure;             // e.g. "binary_heap", "hash_map", "3d_array"
    std::string          driver_abbreviation;
    std::string          team_id;
    double               deviation_vs_actual = 0.0; // seconds vs historical (if known)
};

} // namespace f1
