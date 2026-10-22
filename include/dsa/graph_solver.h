#pragma once
#include "solver_base.h"

namespace f1 {
namespace dsa {

enum class GraphAlgo { DIJKSTRA, ASTAR };

// Graph-based solver
// Represents the strategy state space as a DAG and finds the shortest path.
// A* uses a remaining-laps * best-case-pace heuristic to reduce nodes expanded.
class GraphSolver : public SolverBase {
public:
    explicit GraphSolver(GraphAlgo algo = GraphAlgo::ASTAR) : algo_(algo) {}

    StrategyResult solve(const RaceConfig&            config,
                         const std::vector<TyreSpec>& tyres) override;

    std::string name() const override {
        return algo_ == GraphAlgo::ASTAR ? "A*" : "Dijkstra";
    }

private:
    GraphAlgo algo_;

    // A* heuristic: lower bound on cost from state to finish
    double heuristic(const RaceConfig& config,
                     const std::vector<TyreSpec>& tyres,
                     const StrategyState& state) const;
};

} // namespace dsa
} // namespace f1
