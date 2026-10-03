#include <cmath>
#include <cstdio>

#include "core.hpp"

// Unlike assert(), stays active under NDEBUG and says what failed.
#define CHECK(condition)                                                     \
    if (!(condition)) {                                                      \
        std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
            #condition);                                                     \
        return 1;                                                            \
    }

template<typename Fun>
bool throws(Fun fun) {
    try {
        fun();
    } catch (const std::invalid_argument &) {
        return true;
    }
    return false;
}

int main() {
    const int n = 100, ndays = 10;
    const std::vector<std::string> states = {"Susceptible", "Infected", "Recovered"};
    const auto result = epiworldjs::run_sirconn(n, 0.05, ndays, 42);

    // One row per day and state, in order
    const std::size_t nrows = states.size() * (ndays + 1);
    CHECK(result.day.size() == nrows);
    CHECK(result.state.size() == nrows);
    CHECK(result.count.size() == nrows);
    for (std::size_t i = 0; i < nrows; ++i) {
        CHECK(result.day[i] == static_cast<int>(i / states.size()));
        CHECK(result.state[i] == states[i % states.size()]);
    }

    // Every day accounts for every agent, and the epidemic moves
    for (std::size_t i = 0; i < nrows; i += states.size())
        CHECK(result.count[i] + result.count[i + 1] + result.count[i + 2] == n);
    CHECK(result.count[nrows - 1] > 0);

    // Invalid inputs are rejected, not passed on to epiworld
    CHECK(throws([] { epiworldjs::run_sirconn(0, 0.05, 10, 1); }));
    CHECK(throws([] { epiworldjs::run_sirconn(100, std::nan(""), 10, 1); }));
    CHECK(throws([] { epiworldjs::run_sirconn(100, 0.05, 10, -1); }));
    CHECK(throws([] { epiworldjs::run_sirconn(100, 0.05, 10, 1, INFINITY); }));

    return 0;
}
