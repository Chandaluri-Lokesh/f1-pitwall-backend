#pragma once
#include "tyre.h"
#include "race_state.h"
#include <vector>

namespace f1 {

// Returns lap time for a given lap number accounting for tyre deg + fuel load
double lap_time(const RaceConfig& config,
                const TyreSpec&   tyre,
                int               tyre_age,
                int               lap_number);

// Cost of a stint from start_age to end_age on a compound
double stint_cost(const RaceConfig& config,
                  const TyreSpec&   tyre,
                  int               start_lap,
                  int               start_age,
                  int               num_laps);

// Load a set of TyreSpecs for a given circuit from the data/ directory
std::vector<TyreSpec> load_tyre_specs(const std::string& circuit_name);

// Default degradation model specs (used when no data file present)
std::vector<TyreSpec> default_tyre_specs();

} // namespace f1
