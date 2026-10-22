#include "models/degradation.h"
#include <fstream>
#include <stdexcept>

namespace f1 {

double lap_time(const RaceConfig& config,
                const TyreSpec&   tyre,
                int               tyre_age,
                int               lap_number) {
    double fuel_gain = config.fuel_effect_per_lap * lap_number;
    double deg_cost  = tyre_lap_time_delta(tyre, tyre_age);
    return config.base_lap_time - fuel_gain + deg_cost;
}

double stint_cost(const RaceConfig& config,
                  const TyreSpec&   tyre,
                  int               start_lap,
                  int               start_age,
                  int               num_laps) {
    double total = 0.0;
    for (int i = 0; i < num_laps; ++i) {
        total += lap_time(config, tyre, start_age + i, start_lap + i);
    }
    return total;
}

std::vector<TyreSpec> default_tyre_specs() {
    return {
        // compound,            base_delta, deg_lin, deg_quad, min_laps, max_laps
        { Compound::SOFT,       -0.5,       0.08,    0.003,    5,        30  },
        { Compound::MEDIUM,      0.0,       0.05,    0.002,    10,       40  },
        { Compound::HARD,        0.4,       0.03,    0.001,    15,       55  },
    };
}

std::vector<TyreSpec> load_tyre_specs(const std::string& circuit_name) {
    // TODO: parse data/<circuit_name>_tyres.json
    // Falls back to defaults if file not found
    (void)circuit_name;
    return default_tyre_specs();
}

} // namespace f1
