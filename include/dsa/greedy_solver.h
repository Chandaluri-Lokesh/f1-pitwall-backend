#pragma once
#include "solver_base.h"

namespace f1 {
namespace dsa {

// Greedy solver
// Pits as soon as marginal degradation cost exceeds the amortized pit-loss cost.
// Fast, used as a weak baseline.
class GreedySolver : public SolverBase {
public:
    StrategyResult solve(const RaceConfig&            config,
                         const std::vector<TyreSpec>& tyres) override;
    std::string name() const override { return "Greedy"; }
};

} // namespace dsa
} // namespace f1
