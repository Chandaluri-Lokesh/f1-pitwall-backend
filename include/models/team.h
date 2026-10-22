#pragma once
#include <string>

namespace f1 {

struct Team {
    std::string team_id;            // "ferrari", "red_bull", "mercedes" …
    std::string team_name;          // "Ferrari", "Red Bull Racing" …
    std::string color;              // hex color for UI

    // Strategy-relevant parameters
    double pit_crew_time;           // average stationary pit stop time (seconds)
                                    // effective_pit_loss = circuit_pit_loss - (3.0 - pit_crew_time)
    double pit_crew_reliability;    // probability of a clean stop (1.0 = perfect)
};

// Returns known pit crew parameters for a team_id
// Falls back to mid-field defaults if team not found
Team default_team_params(const std::string& team_id);

// Effective pit lane loss given circuit loss + team crew speed
double effective_pit_loss(double circuit_pit_loss, const Team& team);

} // namespace f1
