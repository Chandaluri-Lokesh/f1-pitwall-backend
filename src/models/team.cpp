#include "models/team.h"
#include <unordered_map>
#include <algorithm>

namespace f1 {

// Pit crew stationary times (seconds) — season averages from 2023-2024 data
// Reference stationary time for the model: 2.5s  (approx midfield)
// effective_pit_loss = circuit_loss - (2.5 - team.pit_crew_time)
// i.e. a faster crew (1.9s) saves 0.6s vs reference; slower crew (3.5s) loses 1.0s
static const std::unordered_map<std::string,
    std::pair<double, double>> TEAM_PARAMS = {
//  team_id           crew_time  reliability
    {"red_bull",      {1.90,     0.98}},
    {"ferrari",       {2.20,     0.96}},
    {"mclaren",       {2.10,     0.97}},
    {"mercedes",      {2.40,     0.97}},
    {"aston_martin",  {2.60,     0.95}},
    {"alpine",        {2.80,     0.94}},
    {"williams",      {3.00,     0.93}},
    {"rb",            {2.70,     0.94}},
    {"haas",          {3.20,     0.92}},
    {"sauber",        {3.10,     0.92}},
    // Legacy names
    {"alphatauri",    {2.70,     0.94}},
    {"alfa_romeo",    {3.10,     0.92}},
};

Team default_team_params(const std::string& team_id) {
    Team t;
    t.team_id              = team_id;
    t.team_name            = team_id;
    t.color                = "#888888";
    t.pit_crew_time        = 2.5;   // midfield default
    t.pit_crew_reliability = 0.95;

    auto it = TEAM_PARAMS.find(team_id);
    if (it != TEAM_PARAMS.end()) {
        t.pit_crew_time        = it->second.first;
        t.pit_crew_reliability = it->second.second;
    }
    return t;
}

double effective_pit_loss(double circuit_pit_loss, const Team& team) {
    // Reference crew time: 2.5s. Faster crew saves time, slower crew adds time.
    constexpr double REFERENCE_CREW_TIME = 2.5;
    double adjusted = circuit_pit_loss - (REFERENCE_CREW_TIME - team.pit_crew_time);
    // Clamp to a sane range
    return std::max(15.0, std::min(adjusted, 40.0));
}

} // namespace f1
