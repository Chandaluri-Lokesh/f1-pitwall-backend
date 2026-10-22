#pragma once
#include "solver_base.h"
#include <vector>
#include <memory>
#include <ostream>

namespace f1 {
namespace dsa {

struct HarnessResult {
    std::vector<StrategyResult> results;   // one per solver, same order as solvers
    StrategyResult              best;       // best solution across all solvers
};

// Runs all registered solvers on the same scenario and collects results.
class ComparisonHarness {
public:
    void add_solver(std::unique_ptr<SolverBase> solver);

    HarnessResult run(const RaceConfig&            config,
                      const std::vector<TyreSpec>& tyres);

    // Print a side-by-side results table to the given stream
    static void print_table(const HarnessResult& result, std::ostream& out);

private:
    std::vector<std::unique_ptr<SolverBase>> solvers_;
};

} // namespace dsa
} // namespace f1
