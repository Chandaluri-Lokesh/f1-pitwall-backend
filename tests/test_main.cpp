// Minimal test runner — no external framework needed
// Each test file registers tests via a static initializer pattern.
#include <iostream>
#include <vector>
#include <functional>
#include <string>

struct Test {
    std::string name;
    std::function<void()> fn;
};

static std::vector<Test>& all_tests() {
    static std::vector<Test> v;
    return v;
}

void register_test(const std::string& name, std::function<void()> fn) {
    all_tests().push_back({name, fn});
}

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "FAIL: " << #a << " != " << #b \
                  << " (" << (a) << " vs " << (b) << ")\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    double _d = (a) - (b); if (_d < 0) _d = -_d; \
    if (_d > (eps)) { \
        std::cerr << "FAIL: |" << #a << " - " << #b << "| > " << (eps) \
                  << " (diff=" << _d << ")\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        std::cerr << "FAIL: " << #x << " is false\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

int main() {
    int passed = 0, failed = 0;
    for (auto& t : all_tests()) {
        try {
            t.fn();
            std::cout << "[PASS] " << t.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed.\n";
    return failed > 0 ? 1 : 0;
}
