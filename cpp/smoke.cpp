#include <cstdio>
#include <set>

#include "core.hpp"

// Unlike assert(), stays active under NDEBUG and says what failed.
#define CHECK(condition)                                                     \
    if (!(condition)) {                                                      \
        std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
            #condition);                                                     \
        return 1;                                                            \
    }

// Whether `fun` throws std::invalid_argument with `text` in its message.
template<typename Fun>
bool throws(Fun fun, const std::string & text) {
    try {
        fun();
    } catch (const std::invalid_argument & e) {
        if (std::string(e.what()).find(text) != std::string::npos)
            return true;
        std::fprintf(stderr, "Unexpected message: %s\n", e.what());
    }
    return false;
}

int main() {
    using epiworldjs::RunSpec;

    // Every model runs with its defaults, and every day accounts for every agent
    for (const auto & info : epiworldjs::registry()) {
        RunSpec spec;
        spec.model = info.id;
        spec.population.n = 300;
        spec.ndays = 20;
        spec.nsims = 2;

        const auto out = epiworldjs::run_many(spec);
        const auto & t = out.at("total_hist");
        CHECK(t.colnames == (std::vector<std::string>{"sim_id", "date", "nviruses", "status", "stage", "counts"}));

        // One row per status and stage a day, and every day accounts for every agent
        const auto & stages = std::get<std::vector<std::string>>(t.columns[4]);
        const auto & counts = std::get<std::vector<int>>(t.columns[5]);
        const auto labels = epiworldjs::labels(info);
        const size_t k = labels.stages.size() * labels.statuses.size();
        CHECK(t.nrow() == 2u * 21u * k);
        const std::set<std::string> all(labels.stages.begin(), labels.stages.end());
        for (size_t i = 0; i < t.nrow(); i += k) {
            int total = 0;
            std::set<std::string> seen;
            for (size_t s = 0; s < k; ++s) {
                seen.insert(stages[i + s]);
                total += counts[i + s];
            }
            CHECK(seen == all);
            if (total != 300)
                std::fprintf(stderr, "%s: %d agents on row %zu\n", info.id.c_str(), total, i);
            CHECK(total == 300);
        }

        // Each argument reaches the model parameter it names. Values spread
        // over each slider's range, different for every argument, make swaps
        // visible.
        for (size_t i = 0; i < info.params.size(); ++i) {
            const auto & p = info.params[i];
            double value = p.min + (p.slider_max - p.min) * (i + 1.0) / (info.params.size() + 1.0);
            spec.params[p.arg] = p.integer ? std::floor(value) : value;
        }
        auto model = epiworldjs::build_model(spec);
        // epiworld stores parameters as epiworld_double (float)
        for (const auto & p : info.params) {
            const std::string & name = p.name;
            const auto value = static_cast<epiworld_double>(spec.params.at(p.arg));
            if (model->par(name) != value)
                std::fprintf(stderr, "%s: \"%s\" is %g, not %g\n", info.id.c_str(),
                    name.c_str(), model->par(name), value);
            CHECK(model->par(name) == value);
        }
    }

    // Invalid specs are rejected with a message naming the problem
    RunSpec ok;
    ok.model = "SIRCONN";
    ok.population.n = 100;
    auto with = [&](auto change) { RunSpec s = ok; change(s); return [s] { epiworldjs::run_many(s); }; };

    CHECK(throws(with([](RunSpec & s) { s.model = "SIRX"; }), "Unknown model \"SIRX\""));
    CHECK(throws(with([](RunSpec & s) { s.params["Contact Rate"] = 2; }), "Unknown parameter \"Contact Rate\""));
    CHECK(throws(with([](RunSpec & s) { s.params["Contact rate"] = 2; }), "\"Contact rate\" is now \"contact_rate\""));
    CHECK(throws(with([](RunSpec & s) { s.params["transmission_prob"] = 1.5; }), "\"transmission_prob\" must be between 0 and 1"));
    CHECK(throws(with([](RunSpec & s) { s.params["recovery_rate"] = std::nan(""); }), "\"recovery_rate\" must be between"));
    CHECK(throws(with([](RunSpec & s) { s.prevalence = -0.1; }), "prevalence must be between 0 and 1"));
    CHECK(throws(with([](RunSpec & s) { s.population.n = 0; }), "n must be greater than 0"));
    CHECK(throws(with([](RunSpec & s) { s.seed = -1; }), "seed must be greater"));
    CHECK(throws(with([](RunSpec & s) { s.nsims = 0; }), "nsims must be at least 1"));
    CHECK(throws(with([](RunSpec & s) { s.outputs = {"total"}; }), "Unknown output \"total\""));
    CHECK(throws(with([](RunSpec & s) { s.population.type = "smallworld"; }), "fully mixed"));
    CHECK(throws([&] { epiworldjs::run_many(ok, {5}); }, "Simulation ids"));

    RunSpec quarantine = ok;
    quarantine.model = "SEIRNetworkQuarantine";
    quarantine.params["quarantine_period"] = 2.5;
    CHECK(throws([&] { epiworldjs::run_many(quarantine); }, "\"quarantine_period\" must be an integer"));

    RunSpec mixing = ok;
    mixing.model = "SIRMixing";
    mixing.population = {};
    mixing.population.sizes = {50, 50};
    mixing.population.contact_matrix = {1, 2, 3};
    CHECK(throws([&] { epiworldjs::run_many(mixing); }, "contact_matrix must be 2 x 2"));

    // Custom groups: rows of the contact matrix are given per group
    mixing.population.contact_matrix = {10, 0, 0, 10};
    CHECK(epiworldjs::run_many(mixing).at("total_hist").nrow() == 3u * 101u);

    return 0;
}
