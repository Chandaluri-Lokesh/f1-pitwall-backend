#include "models/tyre.h"
#include <stdexcept>
#include <cmath>

namespace f1 {

std::string compound_name(Compound c) {
    switch (c) {
        case Compound::SOFT:         return "SOFT";
        case Compound::MEDIUM:       return "MEDIUM";
        case Compound::HARD:         return "HARD";
        case Compound::INTERMEDIATE: return "INTERMEDIATE";
        case Compound::WET:          return "WET";
    }
    return "UNKNOWN";
}

Compound compound_from_string(const std::string& s) {
    if (s == "SOFT")         return Compound::SOFT;
    if (s == "MEDIUM")       return Compound::MEDIUM;
    if (s == "HARD")         return Compound::HARD;
    if (s == "INTERMEDIATE") return Compound::INTERMEDIATE;
    if (s == "WET")          return Compound::WET;
    throw std::invalid_argument("Unknown compound: " + s);
}

double tyre_lap_time_delta(const TyreSpec& spec, int tyre_age) {
    // Quadratic degradation model: delta = base + linear*age + quadratic*age^2
    return spec.base_lap_time_delta
         + spec.deg_linear    * tyre_age
         + spec.deg_quadratic * tyre_age * tyre_age;
}

} // namespace f1
