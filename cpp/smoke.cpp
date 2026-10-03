#include <cassert>
#include <numeric>

#include "core.hpp"

int main() {
    const auto result = epiworldjs::run_sirconn(100, 0.05, 10, 42);
    assert(!result.day.empty());
    assert(result.day.size() == result.state.size());
    assert(result.day.size() == result.count.size());

    // Every date has one count for each SIR state, and totals conserve people.
    for (std::size_t i = 0; i < result.count.size(); i += 3)
        assert(result.count[i] + result.count[i + 1] + result.count[i + 2] == 100);

    return 0;
}

