// Prints every output of a fixed set of runs (all registered models) as text.
// `make golden` builds this natively and with Emscripten and requires the two
// outputs to be identical, and test/golden.test.js replays each run (from the
// spec in its header) through the JS API against the native output. It also
// checks, in either build, that running the simulations in slices (as the
// worker pool does) reproduces run_multiple().

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "core.hpp"

using epiworld::OutputTable;
using epiworld::RunOutputs;

namespace {

// %.17g round-trips every double, so equal text means equal values
std::string number(double x) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.17g", x);
    return buffer;
}

std::string cell(const OutputTable::Column & column, size_t i) {
    return std::visit([i](const auto & values) -> std::string {
        using T = typename std::decay_t<decltype(values)>::value_type;
        if constexpr (std::is_same_v<T, std::string>) {
            return values[i];
        } else if constexpr (std::is_same_v<T, double>) {
            return number(values[i]);
        } else {
            return std::to_string(values[i]);
        }
    }, column);
}

// Rows of each output, keyed by output name and sim_id (the first column)
using Rows = std::map<std::string, std::map<std::string, std::vector<std::string>>>;

Rows rows_by_sim(const RunOutputs & outputs) {
    Rows rows;
    for (const auto & [name, table] : outputs)
        for (size_t i = 0; i < table.nrow(); ++i) {
            std::string row;
            for (size_t j = 1; j < table.columns.size(); ++j)
                row += (j > 1 ? " " : "") + cell(table.columns[j], i);
            rows[name][cell(table.columns[0], i)].push_back(row);
        }
    return rows;
}

void print(const RunOutputs & outputs) {
    for (const auto & [name, table] : outputs) {
        std::printf("## %s\n", name.c_str());
        for (size_t j = 0; j < table.colnames.size(); ++j)
            std::printf("%s%s", j ? "\t" : "", table.colnames[j].c_str());
        std::printf("\n");
        for (size_t i = 0; i < table.nrow(); ++i) {
            for (size_t j = 0; j < table.columns.size(); ++j)
                std::printf("%s%s", j ? "\t" : "", cell(table.columns[j], i).c_str());
            std::printf("\n");
        }
    }
}

// The spec as the JS API takes it (names and values need no escaping)
std::string json(const epiworldjs::RunSpec & spec) {
    auto list = [](const auto & values, auto format) {
        std::string out;
        for (const auto & v : values)
            out += (out.empty() ? "" : ",") + format(v);
        return "[" + out + "]";
    };
    auto quote = [](const std::string & x) { return "\"" + x + "\""; };
    auto integer = [](int x) { return std::to_string(x); };

    std::string params;
    for (const auto & [name, value] : spec.params)
        params += (params.empty() ? "" : ",") + quote(name) + ":" + number(value);

    const auto & pop = spec.population;
    std::string population = "\"n\":" + std::to_string(pop.n);
    if (!pop.type.empty())
        population += ",\"type\":" + quote(pop.type) +
            ",\"source\":" + list(pop.source, integer) + ",\"target\":" + list(pop.target, integer);

    return "{\"model\":" + quote(spec.model) + ",\"params\":{" + params + "}" +
        ",\"prevalence\":" + number(*spec.prevalence) +
        ",\"ndays\":" + std::to_string(spec.ndays) + ",\"nsims\":" + std::to_string(spec.nsims) +
        ",\"seed\":" + std::to_string(spec.seed) + ",\"outputs\":" + list(spec.outputs, quote) +
        ",\"population\":{" + population + "}}";
}

std::vector<epiworldjs::RunSpec> specs() {
    std::vector<epiworldjs::RunSpec> out;
    for (const auto & info : epiworldjs::registry()) {
        epiworldjs::RunSpec spec;
        spec.model = info.id;
        spec.population.n = 600;
        spec.ndays = 30;
        spec.prevalence = 0.05;
        spec.seed = 1231;
        spec.nsims = 5;
        spec.outputs = epiworld::run_output_names();
        out.push_back(spec);

        // Again with every parameter halfway between its default and minimum
        for (const auto & p : info.params) {
            double value = (p.value + p.min) / 2.0;
            spec.params[p.name] = p.integer ? std::floor(value) : value;
        }
        spec.seed = 82;
        out.push_back(spec);
    }

    // A user-supplied network: a ring of 200 agents with chords
    epiworldjs::RunSpec ring;
    ring.model = "SEIRD";
    ring.population.type = "edgelist";
    ring.population.n = 200;
    for (int i = 0; i < 200; ++i) {
        ring.population.source.insert(ring.population.source.end(), {i, i});
        ring.population.target.insert(ring.population.target.end(), {(i + 1) % 200, (i + 37) % 200});
    }
    ring.ndays = 40;
    ring.prevalence = 0.05;
    ring.seed = 7;
    ring.nsims = 3;
    ring.outputs = {"total_hist", "transition", "transmission"};
    out.push_back(ring);

    return out;
}

} // namespace

int main() {
    int failures = 0;

    for (const auto & spec : specs()) {
        const RunOutputs reference = epiworldjs::run_multiple(spec);

        std::printf("# %s\n", json(spec).c_str());
        print(reference);

        // Even and odd simulations in separate calls, as two workers would
        std::vector<int> even, odd;
        for (int i = 0; i < spec.nsims; ++i)
            (i % 2 ? odd : even).push_back(i);

        Rows split = rows_by_sim(epiworldjs::run_many(spec, even));
        for (auto & [name, sims] : rows_by_sim(epiworldjs::run_many(spec, odd)))
            split[name].insert(sims.begin(), sims.end());

        if (split != rows_by_sim(reference)) {
            std::fprintf(stderr, "%s: run_many() in slices differs from run_multiple().\n",
                spec.model.c_str());
            ++failures;
        }
    }

    return failures == 0 ? 0 : 1;
}
