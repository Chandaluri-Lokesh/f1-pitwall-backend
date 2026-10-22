#pragma once
#include <string>
#include <chrono>
#include <vector>

namespace f1 {
namespace coa {

struct ProfileRecord {
    std::string label;
    double      wall_ms;
    long long   instructions;   // from perf counters if available, else -1
    long long   cache_misses;
    long long   branch_misses;
    double      ipc;            // instructions per cycle, -1 if unavailable
};

class Profiler {
public:
    void   start(const std::string& label);
    void   stop();
    void   print_summary() const;
    const  std::vector<ProfileRecord>& records() const { return records_; }

private:
    std::string                     current_label_;
    std::chrono::steady_clock::time_point start_time_;
    std::vector<ProfileRecord>      records_;
};

} // namespace coa
} // namespace f1
