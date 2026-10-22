#pragma once
#include "solver_base.h"

namespace f1 {
namespace dsa {

// Dynamic Programming solver
// Tabulates minimum cost to reach each (lap, compound, tyre_age) state
// and reconstructs the optimal stop sequence via backpointers.
class DPSolver : public SolverBase {
public:
    StrategyResult solve(const RaceConfig&            config,
                         const std::vector<TyreSpec>& tyres) override;
    std::string name() const override { return "Dynamic Programming"; }
};

} // namespace dsa
} // namespace f1
