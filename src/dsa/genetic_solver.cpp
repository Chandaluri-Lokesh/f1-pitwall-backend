#include "dsa/genetic_solver.h"
#include "models/degradation.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

// Genetic Algorithm Pit Stop Solver
// ===================================
// Chromosome  : { start_cmp, pit_laps[], pit_cmps[] }
//               pit_laps is kept sorted; pit_cmps[i] is the compound put on at pit_laps[i]
//
// Fitness     : simulate race following the stop sequence; return total time (lower = better)
//               Any chromosome is valid — constraints are soft (no hard pruning).
//               The quadratic degradation naturally penalises staying too long on a tyre.
//
// Selection   : binary tournament
// Crossover   : merge both parents' stop lists, randomly down-sample to valid count
// Mutation    : one of — shift a lap ±k, change a compound, add a stop, remove a stop
// Elitism     : top cfg_.elite_count individuals survive unchanged each generation
//
// states_explored = total fitness evaluations (population_size × generations + initial)

namespace f1 {
namespace dsa {

// ── Chromosome ──────────────────────────────────────────────────────────────
struct Chromosome {
    int              start_cmp;    // index into tyres[]
    std::vector<int> laps;         // pit lap numbers, sorted ascending
    std::vector<int> cmps;         // compound index at each pit stop
    double           fitness = std::numeric_limits<double>::max();
};

// ── Evaluate one chromosome ─────────────────────────────────────────────────
static double evaluate(const Chromosome&            ch,
                        const RaceConfig&            config,
                        const std::vector<TyreSpec>& tyres) {
    const double pit_loss = (config.effective_pit_loss > 0.0)
                          ? config.effective_pit_loss
                          : config.pit_loss_seconds;
    int    cur_cmp  = ch.start_cmp;
    int    tyre_age = 0;
    double total    = 0.0;
    int    si       = 0;   // stop index

    for (int lap = 1; lap <= config.total_laps; ++lap) {
        // Pit stop at this lap?
        if (si < static_cast<int>(ch.laps.size()) && ch.laps[si] == lap) {
            cur_cmp  = ch.cmps[si];
            total   += pit_loss + lap_time(config, tyres[cur_cmp], 0, lap);
            tyre_age = 1;
            ++si;
        } else {
            total   += lap_time(config, tyres[cur_cmp], tyre_age, lap);
            ++tyre_age;
        }
    }
    return total;
}

// ── Random chromosome ────────────────────────────────────────────────────────
static Chromosome random_chromosome(int N, int C, int max_stops,
                                     std::mt19937& rng) {
    Chromosome ch;
    std::uniform_int_distribution<int> cmp_dist(0, C - 1);
    std::uniform_int_distribution<int> ns_dist(0, max_stops);

    ch.start_cmp = cmp_dist(rng);
    int num_stops = ns_dist(rng);

    if (num_stops > 0 && N > 2) {
        // Sample num_stops distinct laps from [2, N-1]
        std::vector<int> pool(N - 2);
        std::iota(pool.begin(), pool.end(), 2);   // [2, 3, ..., N-1]
        std::shuffle(pool.begin(), pool.end(), rng);
        pool.resize(std::min(num_stops, static_cast<int>(pool.size())));
        std::sort(pool.begin(), pool.end());
        ch.laps = pool;
        ch.cmps.resize(ch.laps.size());
        for (auto& c : ch.cmps) c = cmp_dist(rng);
    }
    return ch;
}

// ── Binary tournament selection ──────────────────────────────────────────────
static const Chromosome& tournament(const std::vector<Chromosome>& pop,
                                     std::mt19937& rng) {
    std::uniform_int_distribution<int> d(0, static_cast<int>(pop.size()) - 1);
    int a = d(rng), b = d(rng);
    return pop[a].fitness <= pop[b].fitness ? pop[a] : pop[b];
}

// ── Crossover ────────────────────────────────────────────────────────────────
// Merge both parents' stops, then randomly keep a subset.
static Chromosome crossover(const Chromosome& p1, const Chromosome& p2,
                              int C, int max_stops, std::mt19937& rng) {
    Chromosome child;
    std::uniform_int_distribution<int> coin(0, 1);
    child.start_cmp = coin(rng) ? p1.start_cmp : p2.start_cmp;

    // Pool all unique laps from both parents
    std::vector<std::pair<int,int>> pool;   // (lap, compound)
    for (int i = 0; i < static_cast<int>(p1.laps.size()); ++i)
        pool.push_back({p1.laps[i], p1.cmps[i]});
    for (int i = 0; i < static_cast<int>(p2.laps.size()); ++i) {
        bool dup = false;
        for (auto& e : pool) if (e.first == p2.laps[i]) { dup = true; break; }
        if (!dup) pool.push_back({p2.laps[i], p2.cmps[i]});
    }
    std::shuffle(pool.begin(), pool.end(), rng);

    int keep = std::min(static_cast<int>(pool.size()), max_stops);
    if (keep > 0) {
        std::uniform_int_distribution<int> k_dist(0, keep);
        keep = k_dist(rng);
    }
    pool.resize(keep);
    std::sort(pool.begin(), pool.end());

    child.laps.reserve(keep);
    child.cmps.reserve(keep);
    for (auto& [l, c] : pool) { child.laps.push_back(l); child.cmps.push_back(c); }
    return child;
}

// ── Mutation ─────────────────────────────────────────────────────────────────
static void mutate(Chromosome& ch, int N, int C, int max_stops,
                    double rate, std::mt19937& rng) {
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int>     cmp_d(0, C - 1);
    std::uniform_int_distribution<int>     shift_d(-5, 5);

    if (prob(rng) < rate)
        ch.start_cmp = cmp_d(rng);

    for (int i = 0; i < static_cast<int>(ch.laps.size()); ++i) {
        if (prob(rng) < rate) {
            // Shift lap
            int nl = ch.laps[i] + shift_d(rng);
            nl = std::max(2, std::min(N - 1, nl));
            ch.laps[i] = nl;
        }
        if (prob(rng) < rate)
            ch.cmps[i] = cmp_d(rng);
    }

    // Add a random stop
    if (prob(rng) < rate && static_cast<int>(ch.laps.size()) < max_stops && N > 2) {
        std::uniform_int_distribution<int> lap_d(2, N - 1);
        int nl = lap_d(rng);
        ch.laps.push_back(nl);
        ch.cmps.push_back(cmp_d(rng));
    }

    // Remove a random stop
    if (prob(rng) < rate && !ch.laps.empty()) {
        std::uniform_int_distribution<int> rm(0, static_cast<int>(ch.laps.size()) - 1);
        int idx = rm(rng);
        ch.laps.erase(ch.laps.begin() + idx);
        ch.cmps.erase(ch.cmps.begin() + idx);
    }

    // Re-sort and deduplicate laps
    if (ch.laps.size() > 1) {
        std::vector<std::pair<int,int>> pairs;
        for (int i = 0; i < static_cast<int>(ch.laps.size()); ++i)
            pairs.push_back({ch.laps[i], ch.cmps[i]});
        std::sort(pairs.begin(), pairs.end());
        auto last = std::unique(pairs.begin(), pairs.end(),
                                [](auto& a, auto& b){ return a.first == b.first; });
        pairs.erase(last, pairs.end());
        ch.laps.clear(); ch.cmps.clear();
        for (auto& [l, c] : pairs) { ch.laps.push_back(l); ch.cmps.push_back(c); }
    }
}

// ── Main solver ──────────────────────────────────────────────────────────────
StrategyResult GeneticSolver::solve(const RaceConfig&            config,
                                     const std::vector<TyreSpec>& tyres) {
    auto t_start = std::chrono::steady_clock::now();

    StrategyResult result;
    result.algorithm      = name();
    result.data_structure = "population_array";
    result.states_explored = 0;
    result.stops_count     = 0;
    result.total_race_time = std::numeric_limits<double>::max();

    if (tyres.empty() || config.total_laps <= 0) {
        auto t_end = std::chrono::steady_clock::now();
        result.wall_clock_ms =
            std::chrono::duration<double, std::milli>(t_end - t_start).count();
        return result;
    }

    const int N = config.total_laps;
    const int C = static_cast<int>(tyres.size());
    const int POP  = cfg_.population_size;
    const int GENS = cfg_.generations;

    std::mt19937 rng(42);   // fixed seed — deterministic, reproducible

    // ── Initial population ───────────────────────────────────────────────────
    std::vector<Chromosome> pop(POP);
    for (auto& ch : pop) {
        ch = random_chromosome(N, C, cfg_.max_stops, rng);
        ch.fitness = evaluate(ch, config, tyres);
        ++result.states_explored;
    }

    Chromosome best_ever = *std::min_element(pop.begin(), pop.end(),
        [](const Chromosome& a, const Chromosome& b){ return a.fitness < b.fitness; });

    // ── Generational loop ────────────────────────────────────────────────────
    for (int gen = 0; gen < GENS; ++gen) {
        // Sort: elites at front
        std::sort(pop.begin(), pop.end(),
                  [](const Chromosome& a, const Chromosome& b){ return a.fitness < b.fitness; });

        if (pop[0].fitness < best_ever.fitness)
            best_ever = pop[0];

        std::vector<Chromosome> next;
        next.reserve(POP);

        // Elitism: carry top-k unchanged
        for (int i = 0; i < cfg_.elite_count && i < POP; ++i)
            next.push_back(pop[i]);

        // Fill rest
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        while (static_cast<int>(next.size()) < POP) {
            const Chromosome& p1 = tournament(pop, rng);

            Chromosome child;
            if (prob(rng) < cfg_.crossover_rate) {
                const Chromosome& p2 = tournament(pop, rng);
                child = crossover(p1, p2, C, cfg_.max_stops, rng);
            } else {
                child = p1;
            }

            mutate(child, N, C, cfg_.max_stops, cfg_.mutation_rate, rng);
            child.fitness = evaluate(child, config, tyres);
            ++result.states_explored;
            next.push_back(std::move(child));
        }

        pop = std::move(next);
    }

    // Final sort
    std::sort(pop.begin(), pop.end(),
              [](const Chromosome& a, const Chromosome& b){ return a.fitness < b.fitness; });
    if (pop[0].fitness < best_ever.fitness)
        best_ever = pop[0];

    // ── Package result ───────────────────────────────────────────────────────
    result.total_race_time = best_ever.fitness;

    for (int i = 0; i < static_cast<int>(best_ever.laps.size()); ++i)
        result.stops.push_back({ best_ever.laps[i], tyres[best_ever.cmps[i]].compound });

    result.stops_count = static_cast<int>(result.stops.size());

    auto t_end = std::chrono::steady_clock::now();
    result.wall_clock_ms =
        std::chrono::duration<double, std::milli>(t_end - t_start).count();
    return result;
}

} // namespace dsa
} // namespace f1
