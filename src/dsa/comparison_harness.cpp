#include "dsa/comparison_harness.h"
#include <iomanip>
#include <iostream>
#include <algorithm>

namespace f1 {
namespace dsa {

void ComparisonHarness::add_solver(std::unique_ptr<SolverBase> solver) {
    solvers_.push_back(std::move(solver));
}

HarnessResult ComparisonHarness::run(const RaceConfig&            config,
                                      const std::vector<TyreSpec>& tyres) {
    HarnessResult hr;
    hr.best.total_race_time = std::numeric_limits<double>::max();

    for (auto& solver : solvers_) {
        StrategyResult r = solver->solve(config, tyres);
        if (r.total_race_time < hr.best.total_race_time) {
            hr.best = r;
        }
        hr.results.push_back(std::move(r));
    }
    return hr;
}

void ComparisonHarness::print_table(const HarnessResult& hr, std::ostream& out) {
    const int W = 22;
    out << "\n"
        << std::left << std::setw(W) << "Algorithm"
        << std::right
        << std::setw(12) << "Race Time(s)"
        << std::setw(8)  << "Stops"
        << std::setw(14) << "Wall-ms"
        << std::setw(16) << "States Explored"
        << "\n";
    out << std::string(W + 12 + 8 + 14 + 16, '-') << "\n";

    for (const auto& r : hr.results) {
        out << std::left  << std::setw(W) << r.algorithm
            << std::right
            << std::setw(12) << std::fixed << std::setprecision(3) << r.total_race_time
            << std::setw(8)  << r.stops_count
            << std::setw(14) << std::fixed << std::setprecision(2) << r.wall_clock_ms
            << std::setw(16) << r.states_explored
            << "\n";
    }
    out << "\nBest: " << hr.best.algorithm
        << " — " << hr.best.total_race_time << "s\n";
}

} // namespace dsa
} // namespace f1
