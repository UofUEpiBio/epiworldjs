#ifndef EPIWORLDJS_CORE_HPP
#define EPIWORLDJS_CORE_HPP

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "epiworld.hpp"

namespace epiworldjs {

struct SimulationResult {
    std::vector<int> day;
    std::vector<std::string> state;
    std::vector<int> count;
};

// Written as !(in range) so that NaN, which compares false, is rejected too.
inline void require_probability(const char * name, double value) {
    if (!(value >= 0.0 && value <= 1.0))
        throw std::invalid_argument(std::string(name) + " must be between 0 and 1.");
}

// The first bridge deliberately has a narrow interface.  It exercises the real
// epiworld engine end-to-end while the registry in the next roadmap stage grows
// this into a declarative multi-model API.
inline SimulationResult run_sirconn(
    int n,
    double prevalence,
    int ndays,
    int seed,
    double contact_rate = 4.0,
    double transmission_rate = 0.1,
    double recovery_rate = 1.0 / 7.0
) {
    if (n <= 0)
        throw std::invalid_argument("n must be greater than 0.");
    if (ndays < 0)
        throw std::invalid_argument("ndays must be greater than or equal to 0.");
    // epiworld ignores negative seeds, so they would silently not seed the run
    if (seed < 0)
        throw std::invalid_argument("seed must be greater than or equal to 0.");
    require_probability("prevalence", prevalence);
    require_probability("transmission_rate", transmission_rate);
    require_probability("recovery_rate", recovery_rate);
    if (!(std::isfinite(contact_rate) && contact_rate >= 0.0))
        throw std::invalid_argument("contact_rate must be a finite number greater than or equal to 0.");

    epiworld::epimodels::ModelSIRCONN<> model(
        "SARS-CoV-2", static_cast<epiworld_fast_uint>(n), prevalence,
        contact_rate, transmission_rate, recovery_rate
    );
    model.verbose_off();
    model.run(ndays, seed);

    SimulationResult result;
    model.get_db().get_hist_total(&result.day, &result.state, &result.count);
    return result;
}

} // namespace epiworldjs

#endif
