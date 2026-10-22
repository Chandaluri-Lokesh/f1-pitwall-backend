#pragma once
#include "solver_base.h"

namespace f1 {
namespace dsa {

struct GAConfig {
    int    population_size = 100;
    int    generations     = 500;
    double mutation_rate   = 0.05;
    double crossover_rate  = 0.8;
    int    elite_count     = 5;
    int    max_stops       = 4;
};

// Genetic Algorithm / Simulated Annealing solver (stretch goal)
// Chromosome: sequence of (pit_lap, compound) pairs
// For larger search spaces with safety-car uncertainty.
class GeneticSolver : public SolverBase {
public:
    explicit GeneticSolver(GAConfig cfg = {}) : cfg_(cfg) {}

    StrategyResult solve(const RaceConfig&            config,
                         const std::vector<TyreSpec>& tyres) override;
    std::string name() const override { return "Genetic Algorithm"; }

private:
    GAConfig cfg_;
};

} // namespace dsa
} // namespace f1
