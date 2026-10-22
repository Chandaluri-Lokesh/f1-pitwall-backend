#pragma once
#include "models/race_state.h"
#include "models/degradation.h"
#include <vector>
#include <string>

namespace f1 {
namespace dsa {

class SolverBase {
public:
    virtual ~SolverBase() = default;

    // Solve and return the best strategy found
    virtual StrategyResult solve(const RaceConfig&         config,
                                 const std::vector<TyreSpec>& tyres) = 0;

    virtual std::string name() const = 0;
};

} // namespace dsa
} // namespace f1
