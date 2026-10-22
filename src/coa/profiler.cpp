#include "coa/profiler.h"
#include <iostream>
#include <iomanip>

namespace f1 {
namespace coa {

void Profiler::start(const std::string& label) {
    current_label_ = label;
    start_time_    = std::chrono::steady_clock::now();
}

void Profiler::stop() {
    auto end  = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start_time_).count();

    ProfileRecord rec;
    rec.label         = current_label_;
    rec.wall_ms       = ms;
    rec.instructions  = -1;   // populated by perf on Linux
    rec.cache_misses  = -1;
    rec.branch_misses = -1;
    rec.ipc           = -1.0;
    records_.push_back(rec);
}

void Profiler::print_summary() const {
    const int W = 24;
    std::cout << "\n--- Profiler Summary ---\n"
              << std::left  << std::setw(W) << "Label"
              << std::right << std::setw(12) << "Wall(ms)"
              << "\n" << std::string(W + 12, '-') << "\n";
    for (const auto& r : records_) {
        std::cout << std::left  << std::setw(W) << r.label
                  << std::right << std::setw(12) << std::fixed
                  << std::setprecision(3) << r.wall_ms << "\n";
    }
}

} // namespace coa
} // namespace f1
