#ifndef EPIWORLDJS_CORE_HPP
#define EPIWORLDJS_CORE_HPP

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "registry.hpp"

namespace epiworldjs {

/// Where agents come from. Which fields apply depends on the model's
/// `population` kind (see `ModelInfo`); unset fields take the defaults below.
struct Population {
    /// "smallworld" or "edgelist" (network models), "connected", or "groups"
    /// (mixing models). Empty means the model's default.
    std::string type;
    int n = -1;                          ///< Number of agents; -1 = default
    int k = 20;                          ///< Small-world: ties per agent
    double p = 0.2;                      ///< Small-world: rewiring probability
    bool directed = false;               ///< Small-world and edge list
    std::vector<int> source, target;     ///< Edge list, 0-based agent ids
    std::vector<int> sizes;              ///< Groups: agents per group
    std::vector<double> contact_matrix;  ///< Groups: row-major, (i, j) = daily contacts of i with j
};

/// A complete description of a run. Everything a worker needs to reproduce
/// any of the `nsims` simulations is here; `run_many()` picks which ones.
struct RunSpec {
    std::string model;
    std::map<std::string, double> params; ///< Unset parameters take their defaults
    std::optional<double> prevalence; ///< Initial proportion infected; unset = model default
    int ndays = 100;
    int nsims = 1;
    int seed = 1;
    Population population;
    std::vector<std::string> outputs = {"total_hist"}; ///< Names from `run_output_names()`
};

namespace detail {

inline void require(bool ok, const std::string & message) {
    if (!ok)
        throw std::invalid_argument(message);
}

// Limits are short numbers (0, 1, 0.5, 60), which the default format keeps
inline std::string format_number(double x) {
    std::ostringstream out;
    out << x;
    return out.str();
}

// Written as !(in range) so that NaN, which compares false, is rejected too.
inline void require_in(double value, double min, double max, const std::string & what) {
    require(value >= min && value <= max,
        what + " must be between " + format_number(min) + " and " + format_number(max) + ".");
}

} // namespace detail

/// The parameters of `spec`, with defaults filled in. Throws on unknown names
/// and out-of-range values, naming the parameter.
inline std::map<std::string, double> resolve_params(const ModelInfo & info, const RunSpec & spec) {

    for (const auto & kv : spec.params) {
        bool known = std::any_of(info.params.begin(), info.params.end(),
            [&](const ParamInfo & p) { return p.name == kv.first; });
        if (!known) {
            std::string names;
            for (const auto & p : info.params)
                names += (names.empty() ? "\"" : ", \"") + p.name + "\"";
            throw std::invalid_argument("Unknown parameter \"" + kv.first +
                "\" for model " + info.id + ". Its parameters are: " + names + ".");
        }
    }

    std::map<std::string, double> out;
    for (const auto & p : info.params) {
        auto it = spec.params.find(p.name);
        double value = it == spec.params.end() ? p.value : it->second;
        detail::require_in(value, p.min, p.max, "\"" + p.name + "\"");
        detail::require(!p.integer || std::floor(value) == value,
            "\"" + p.name + "\" must be an integer.");
        out[p.name] = value;
    }
    return out;
}

/// `spec.population` with the model's defaults filled in, validated.
inline Population resolve_population(const ModelInfo & info, const RunSpec & spec) {

    using detail::require;
    Population pop = spec.population;

    if (info.population == "network") {
        if (pop.type.empty())
            pop.type = "smallworld";
        require(pop.type == "smallworld" || pop.type == "edgelist",
            "Model " + info.id + " needs a population of type \"smallworld\" or \"edgelist\".");
        if (pop.n < 0)
            pop.n = info.n;
        require(pop.n > 0, "n must be greater than 0.");
        if (pop.type == "smallworld") {
            require(pop.k >= 0 && pop.k < pop.n, "k must be between 0 and n - 1.");
            detail::require_in(pop.p, 0.0, 1.0, "p");
        } else {
            require(pop.source.size() == pop.target.size(),
                "source and target must have the same length.");
            for (size_t i = 0; i < pop.source.size(); ++i)
                require(pop.source[i] >= 0 && pop.source[i] < pop.n &&
                    pop.target[i] >= 0 && pop.target[i] < pop.n,
                    "Edge " + std::to_string(i) + " refers to an agent outside 0..n-1.");
        }
    } else if (info.population == "connected") {
        if (pop.type.empty())
            pop.type = "connected";
        require(pop.type == "connected",
            "Model " + info.id + " is fully mixed; it takes only n, not a population of type \"" + pop.type + "\".");
        if (pop.n < 0)
            pop.n = info.n;
        require(pop.n > 0, "n must be greater than 0.");
    } else { // mixing
        if (pop.type.empty())
            pop.type = "groups";
        require(pop.type == "groups",
            "Model " + info.id + " needs a population of type \"groups\".");
        if (pop.sizes.empty() && pop.contact_matrix.empty()) {
            // The model's default matrix, with n split evenly between groups
            const int groups = static_cast<int>(std::lround(std::sqrt(info.contact_matrix.size())));
            const int n = pop.n < 0 ? info.n : pop.n;
            require(n >= groups, "n must be at least " + std::to_string(groups) + " for the default groups.");
            pop.sizes.assign(static_cast<size_t>(groups), n / groups);
            pop.sizes.back() += n % groups;
            pop.contact_matrix = info.contact_matrix;
        }
        size_t g = pop.sizes.size();
        require(g > 0, "sizes must have at least one group.");
        require(pop.contact_matrix.size() == g * g,
            "contact_matrix must be " + std::to_string(g) + " x " + std::to_string(g) +
            " (one row and column per group).");
        long total = 0;
        for (int s : pop.sizes) {
            require(s > 0, "Every group size must be greater than 0.");
            total += s;
        }
        require(total <= std::numeric_limits<int>::max(), "The groups are too large.");
        require(pop.n < 0 || pop.n == total, "n must equal the sum of the group sizes.");
        pop.n = static_cast<int>(total);
        for (double c : pop.contact_matrix)
            require(std::isfinite(c) && c >= 0.0,
                "contact_matrix entries must be finite and non-negative.");
    }

    return pop;
}

inline void validate_outputs(const std::vector<std::string> & outputs) {
    const auto & names = epiworld::run_output_names();
    for (const auto & o : outputs)
        if (std::find(names.begin(), names.end(), o) == names.end()) {
            std::string known;
            for (const auto & n : names)
                known += (known.empty() ? "" : ", ") + n;
            throw std::invalid_argument("Unknown output \"" + o + "\". Available outputs: " + known + ".");
        }
}

/// Builds the model `spec` describes, with its population, ready to run.
inline std::unique_ptr<Model> build_model(const RunSpec & spec) {

    using detail::require;
    const ModelInfo & info = find_model(spec.model);

    const double prevalence = spec.prevalence.value_or(info.prevalence);
    detail::require_in(prevalence, 0.0, 1.0, "prevalence");
    require(spec.ndays >= 0, "ndays must be greater than or equal to 0.");
    require(spec.nsims >= 1, "nsims must be at least 1.");
    // epiworld ignores negative seeds, so they would silently not seed the run
    require(spec.seed >= 0, "seed must be greater than or equal to 0.");
    validate_outputs(spec.outputs);

    BuildArgs args;
    args.params = resolve_params(info, spec);
    args.prevalence = prevalence;
    Population pop = resolve_population(info, spec);
    args.n = pop.n;
    args.n_infected = static_cast<int>(std::lround(prevalence * pop.n));

    // Row-major (as users write it) to column-major (as epiworld reads it)
    size_t g = pop.sizes.size();
    args.contact_matrix.resize(pop.contact_matrix.size());
    for (size_t i = 0; i < g; ++i)
        for (size_t j = 0; j < g; ++j)
            args.contact_matrix[j * g + i] = pop.contact_matrix[i * g + j];

    auto model = info.build(args);
    model->verbose_off();

    // Seeded so that the random network depends only on the spec
    model->seed(static_cast<size_t>(spec.seed));

    if (pop.type == "smallworld") {
        model->agents_smallworld(
            static_cast<epiworld_fast_uint>(pop.n), static_cast<epiworld_fast_uint>(pop.k),
            pop.directed, pop.p);
    } else if (pop.type == "edgelist") {
        model->agents_from_edgelist(pop.source, pop.target, pop.n, pop.directed);
    } else if (pop.type == "groups") {
        int from = 0;
        for (size_t i = 0; i < g; ++i) {
            model->add_entity(epiworld::Entity<>("Group " + std::to_string(i + 1),
                epiworld::distribute_entity_to_range<>(from, from + pop.sizes[i])));
            from += pop.sizes[i];
        }
    }

    return model;
}

/// The per-simulation seeds, drawn as `Model::run_multiple()` draws them.
inline std::vector<int> draw_sim_seeds(Model & model, int seed, int nsims) {
    model.seed(static_cast<size_t>(seed));
    std::vector<int> seeds(static_cast<size_t>(nsims));
    for (auto & s : seeds)
        s = static_cast<int>(std::floor(
            model.runif() * static_cast<double>(std::numeric_limits<int>::max())));
    return seeds;
}

/**
 * Runs some of the simulations of `spec` (all of them if `sim_ids` is empty)
 * and returns their outputs, concatenated with a leading `sim_id` column.
 *
 * Each simulation gets the seed `run_multiple()` would give it, so splitting
 * `0..nsims-1` across calls (or workers) and concatenating the results by
 * `sim_id` reproduces `run_multiple(ndays, nsims, seed, ...)` exactly.
 */
inline epiworld::RunOutputs run_many(const RunSpec & spec, std::vector<int> sim_ids = {}) {

    auto model = build_model(spec);
    const auto seeds = draw_sim_seeds(*model, spec.seed, spec.nsims);

    if (sim_ids.empty())
        for (int i = 0; i < spec.nsims; ++i)
            sim_ids.push_back(i);
    for (int id : sim_ids)
        detail::require(id >= 0 && id < spec.nsims,
            "Simulation ids must be between 0 and nsims - 1.");

    model->set_backup();
    epiworld::SaverMemory saver(spec.outputs);
    // Unlike run_multiple(), this leaves the model's sim_id alone (setting it
    // is protected); epiworld only uses it to tell runs apart, and run()
    // already increments it.
    for (int id : sim_ids) {
        model->run(static_cast<epiworld_fast_uint>(spec.ndays), seeds[static_cast<size_t>(id)]);
        saver(static_cast<size_t>(id), model.get());
    }
    return saver.results();
}

/// The same as `run_many(spec)`, through epiworld's own `run_multiple()`.
/// It is the reference the golden tests compare `run_many()` against.
inline epiworld::RunOutputs run_multiple(const RunSpec & spec) {
    auto model = build_model(spec);
    epiworld::SaverMemory saver(spec.outputs);
    model->run_multiple(static_cast<epiworld_fast_uint>(spec.ndays),
        static_cast<epiworld_fast_uint>(spec.nsims), spec.seed, saver, true, false, 1);
    return saver.results();
}

/// The state labels of a model, in epiworld's order.
inline std::vector<std::string> state_labels(const ModelInfo & info) {
    RunSpec spec;
    spec.model = info.id;
    // The smallest population each kind allows, since only the states matter
    spec.population.n = 3;
    spec.population.k = 1;
    return build_model(spec)->get_states();
}

} // namespace epiworldjs

#endif
