#pragma once
#include <string>

namespace f1 {

struct Driver {
    std::string abbreviation;       // "LEC", "VER", "HAM" …
    std::string full_name;
    int         number;
    std::string team_id;
    std::string nationality;

    // Strategy-relevant parameters
    double tyre_mgmt_factor;        // multiplier on deg_linear: < 1.0 = better management
                                    // e.g. 0.92 = driver extends tyres 8% beyond model
    double qualifying_delta;        // seconds vs field average in qualifying trim (not used in race solver)
};

// Default tyre management factors derived from historical stint analysis
// (will be overridden by DB values once we add per-driver historical fitting)
Driver default_driver_params(const std::string& abbreviation);

} // namespace f1
