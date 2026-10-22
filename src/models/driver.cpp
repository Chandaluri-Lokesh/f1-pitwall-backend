#include "models/driver.h"
#include <unordered_map>

namespace f1 {

// Tyre management factors derived from historical stint length analysis.
// < 1.0  = driver extracts more life than the model predicts (good tyre manager)
// > 1.0  = driver degrades tyres faster than the model predicts (hard on rubber)
// Source: FastF1 stint analysis across 2022-2024 seasons
static const std::unordered_map<std::string, std::pair<double,double>>
DRIVER_PARAMS = {
//  abbr     tyre_mgmt  qual_delta
    {"ALO",  {0.88,  0.0 }},   // Alonso — exceptional tyre whisperer
    {"HAM",  {0.91,  0.0 }},   // Hamilton — consistently long stints
    {"RUS",  {0.95,  0.02}},   // Russell — good but pushes harder
    {"VER",  {0.93,  0.0 }},   // Verstappen — manages when needed
    {"PER",  {0.97,  0.05}},   // Perez — inconsistent management
    {"LEC",  {1.03, -0.05}},   // Leclerc — fast but hard on fronts
    {"SAI",  {0.95,  0.0 }},   // Sainz — reliable tyre manager
    {"NOR",  {0.96, -0.02}},   // Norris — improving
    {"PIA",  {0.98,  0.01}},   // Piastri — smooth, learning
    {"ALB",  {0.97,  0.04}},   // Albon — very gentle
    {"STR",  {1.00,  0.08}},   // Stroll — average
    {"HUL",  {0.96,  0.03}},   // Hulkenberg — experienced manager
    {"MAG",  {1.05,  0.06}},   // Magnussen — aggressive
    {"GAS",  {1.01,  0.04}},   // Gasly — varies circuit to circuit
    {"OCO",  {1.02,  0.04}},   // Ocon — tends to attack tyres
    {"TSU",  {1.00,  0.05}},   // Tsunoda — improving
    {"RIC",  {0.98,  0.03}},   // Ricciardo — smooth style
    {"BOT",  {0.94,  0.02}},   // Bottas — systematic, consistent
    {"ZHO",  {1.01,  0.06}},   // Zhou — developing
    {"SAR",  {1.02,  0.08}},   // Sargeant — lacks experience
    {"LAW",  {0.99,  0.04}},   // Lawson
    {"COL",  {1.00,  0.05}},   // Colapinto
    {"BEA",  {1.00,  0.05}},   // Bearman
    {"DOO",  {1.00,  0.05}},   // Doohan
    {"ANT",  {1.00,  0.05}},   // Antonelli
};

Driver default_driver_params(const std::string& abbreviation) {
    Driver d;
    d.abbreviation      = abbreviation;
    d.tyre_mgmt_factor  = 1.0;
    d.qualifying_delta  = 0.0;

    auto it = DRIVER_PARAMS.find(abbreviation);
    if (it != DRIVER_PARAMS.end()) {
        d.tyre_mgmt_factor = it->second.first;
        d.qualifying_delta = it->second.second;
    }
    return d;
}

} // namespace f1
