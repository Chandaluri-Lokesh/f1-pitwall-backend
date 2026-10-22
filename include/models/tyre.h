#pragma once
#include <string>

namespace f1 {

enum class Compound {
    SOFT,
    MEDIUM,
    HARD,
    INTERMEDIATE,
    WET
};

std::string compound_name(Compound c);
Compound    compound_from_string(const std::string& s);

struct TyreSpec {
    Compound    compound;
    double      base_lap_time_delta;  // seconds vs reference pace
    double      deg_linear;           // seconds lost per lap (linear term)
    double      deg_quadratic;        // seconds lost per lap^2 (quadratic term)
    int         min_laps;             // mandatory minimum stint length
    int         max_laps;             // practical cliff threshold
};

// Returns the additional lap time (seconds) for a tyre at given age
double tyre_lap_time_delta(const TyreSpec& spec, int tyre_age);

} // namespace f1
