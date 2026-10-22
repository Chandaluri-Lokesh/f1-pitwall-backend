#pragma once
#include "solver_base.h"

namespace f1 {
namespace dsa {

// Branch & Bound solver
// Exhaustively explores stop-lap and compound combinations,
// pruning branches whose lower-bound cost already exceeds the current best.
class BranchBoundSolver : public SolverBase {
public:
    StrategyResult solve(const RaceConfig&            config,
                         const std::vector<TyreSpec>& tyres) override;
    std::string name() const override { return "Branch & Bound"; }

private:
    void branch(const RaceConfig&            config,
                const std::vector<TyreSpec>& tyres,
                int                          lap,
                Compound                     compound,
                int                          tyre_age,
                double                       cost_so_far,
                std::vector<PitStop>&        current_stops,
                StrategyResult&              best);
};

} // namespace dsa
} // namespace f1
