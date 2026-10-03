#include <emscripten/bind.h>

#include "core.hpp"

using namespace emscripten;

EMSCRIPTEN_BINDINGS(epiworldjs) {
    register_vector<int>("IntVector");
    register_vector<std::string>("StringVector");

    value_object<epiworldjs::SimulationResult>("SimulationResult")
        .field("day", &epiworldjs::SimulationResult::day)
        .field("state", &epiworldjs::SimulationResult::state)
        .field("count", &epiworldjs::SimulationResult::count);

    function("runSIRCONN", &epiworldjs::run_sirconn);
}

