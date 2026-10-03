#include <climits>
#include <cmath>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "core.hpp"

using emscripten::val;

namespace {

// JS numbers arrive as doubles; this rejects NaN, fractions, and values that
// would wrap around when converted to int.
int to_int(double value, const char * name) {
    if (!(value >= INT_MIN && value <= INT_MAX) || std::floor(value) != value)
        throw std::invalid_argument(std::string(name) + " must be an integer.");
    return static_cast<int>(value);
}

// Copies into arrays that JS owns, so callers have nothing to free.
val to_js(const std::vector<int> & values) {
    return val::global("Int32Array").new_(
        emscripten::typed_memory_view(values.size(), values.data())
    );
}

val to_js(const std::vector<std::string> & values) {
    val array = val::array();
    for (const auto & value : values)
        array.call<void>("push", value);
    return array;
}

val run_sirconn(
    double n,
    double prevalence,
    double ndays,
    double seed,
    double contact_rate,
    double transmission_rate,
    double recovery_rate
) {
    try {
        const auto result = epiworldjs::run_sirconn(
            to_int(n, "n"), prevalence, to_int(ndays, "ndays"),
            to_int(seed, "seed"), contact_rate, transmission_rate, recovery_rate
        );

        val out = val::object();
        out.set("day", to_js(result.day));
        out.set("state", to_js(result.state));
        out.set("count", to_js(result.count));
        return out;
    } catch (const std::exception & e) {
        // Surfaces as a JS Error carrying epiworld's message
        val::global("Error").new_(std::string(e.what())).throw_();
    }
}

} // namespace

EMSCRIPTEN_BINDINGS(epiworldjs) {
    emscripten::function("runSIRCONN", &run_sirconn);
}
