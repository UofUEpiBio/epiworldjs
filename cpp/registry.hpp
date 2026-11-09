#ifndef EPIWORLDJS_REGISTRY_HPP
#define EPIWORLDJS_REGISTRY_HPP

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "epiworld.hpp"
#include "measles/measles.hpp"

namespace epiworldjs {

using Model = epiworld::Model;

/// The core's registry: epiworld's models and the measles models. Each one
/// lists its arguments (`epiworld::ParamInfo`) and builds the model.
inline const epiworld::ModelRegistry & core_registry() {
    static const epiworld::ModelRegistry registry = [] {
        epiworld::ModelRegistry r;
        epiworld::epimodels::register_models(r);
        measles::register_models(r);
        return r;
    }();
    return registry;
}

/// One argument of a preset: the core's `ParamInfo` (`arg` is the name a spec
/// uses, `name` the model parameter it sets), plus a slider's step and span.
/// `min`/`max` are the hard limits, checked before the model is built; `max`
/// may be infinite, so a slider spans [min, slider_max].
struct ParamInfo {
    std::string arg;
    std::string name;
    double value;
    double min;
    double max;
    double step;
    double slider_max;
    bool integer;
    std::string description;
};

/// What a preset's layer receives: the preset's own arguments (those the core
/// model does not take) and the population.
struct BuildArgs {
    std::map<std::string, double> params;
    int n;
    std::vector<double> contact_matrix; ///< Column-major, as epiworld expects
    std::vector<int> sizes; ///< Group sizes ("mixing" population), in agent order

    double operator()(const std::string & arg) const { return params.at(arg); }
};

using AddLayers = std::function<void(Model &, const BuildArgs &)>;

/// A preset: a model of the core's registry with the population it runs on.
/// How agents meet decides what `population` a spec can take:
/// - "network": a small-world network or an edge list, added after the model;
/// - "connected": random mixing, only `n` (the preset or the model adds the
///   layer);
/// - "mixing": groups of given sizes and a contact matrix (the preset adds
///   the layer).
struct ModelInfo {
    std::string id;
    std::string label;
    std::string family;
    std::string population;
    std::string model; ///< Its id in the core's registry
    /// The core model's arguments other than `n` and `prevalence`, then the
    /// preset's own, with the preset's defaults
    std::vector<ParamInfo> params;
    std::set<std::string> extra; ///< The preset's own arguments (its layer's)
    int n = 0;
    double prevalence = 0.0;
    /// The core model takes the initial cases as a count (MeaslesSchool): a
    /// spec's prevalence becomes round(prevalence * n)
    bool prevalence_count = false;
    /// Adds the preset's layer (none for "network": core.hpp adds it)
    AddLayers add_layers;
    /// Mixing presets: daily contacts between three equal groups, row-major
    std::vector<double> contact_matrix = {
        18.0, 1.0, 1.0,
         2.0, 16.0, 2.0,
         2.0, 4.0, 14.0
    };
};

namespace detail {

// A slider's step and span, from the argument's kind
inline ParamInfo with_slider(const epiworld::ParamInfo & p) {
    const bool rate = p.arg == "contact_rate";
    const bool window = p.arg.find("window") != std::string::npos;
    const double step = p.integer ? 1.0 : (p.max <= 1.0 ? 0.01 : (rate ? 0.1 : 0.5));
    const double slider_max = p.max <= 1.0 ? p.max : (rate ? 100.0 : (window ? 30.0 : 60.0));
    return {p.arg, p.name, p.value, p.min, p.max, step, slider_max, p.integer, p.description};
}

// Random mixing: everyone in one group, meeting "Contact rate" others a day
inline void random_mixing(Model & model, const BuildArgs & a) {
    model.add_param(a("contact_rate"), "Contact rate");
    auto mix = epiworld::layer::mixing("mixing");
    mix->set_groups(epiworld::assign::labels(std::vector<int>(static_cast<size_t>(a.n), 0)), 1u);
    mix->set_contact_matrix({1.0});
    mix->set_contact_rate("Contact rate");
    model.add_layer(mix);
}

// Groups of consecutive agents mixing by the contact matrix
inline void group_mixing(Model & model, const BuildArgs & a) {
    std::vector<int> groups;
    for (size_t g = 0; g < a.sizes.size(); ++g)
        groups.insert(groups.end(), static_cast<size_t>(a.sizes[g]), static_cast<int>(g));
    auto mix = epiworld::layer::mixing("mixing");
    mix->set_groups(epiworld::assign::labels(std::move(groups)), a.sizes.size());
    mix->set_contact_matrix(a.contact_matrix);
    model.add_layer(mix);
}

/**
 * A preset of the core model `model`. Its arguments, defaults, limits and
 * descriptions are the core's; `defaults` changes some defaults, `hidden`
 * leaves out arguments the model ignores, and `extra` adds the arguments of
 * the preset's layer.
 */
inline ModelInfo preset(
    std::string id, std::string label, std::string family, std::string population,
    std::string model,
    const std::map<std::string, double> & defaults = {},
    const std::set<std::string> & hidden = {},
    const std::vector<epiworld::ParamInfo> & extra = {},
    AddLayers add_layers = nullptr
) {
    ModelInfo m;
    m.id = std::move(id);
    m.label = std::move(label);
    m.family = std::move(family);
    m.population = std::move(population);
    m.model = std::move(model);
    m.add_layers = std::move(add_layers);

    for (const auto & p : core_registry().get_model_info(m.model).params) {
        if (p.arg == "n")
            m.n = static_cast<int>(p.value);
        else if (p.arg == "prevalence") {
            m.prevalence_count = p.integer;
            m.prevalence = p.integer ? p.value / m.n : p.value;
        } else if (!hidden.count(p.arg))
            m.params.push_back(with_slider(p));
    }
    for (const auto & p : extra) {
        m.params.push_back(with_slider(p));
        m.extra.insert(p.arg);
    }
    for (auto & p : m.params)
        if (defaults.count(p.arg))
            p.value = defaults.at(p.arg);
    return m;
}

// One initial case among three groups of 3000, 15 contacts per day (the
// examples of the measles R package)
inline ModelInfo measles_mixing(ModelInfo m) {
    m.contact_matrix = {
        13.5, 0.75, 0.75,
         1.5, 12.0,  1.5,
         1.5,  3.0, 10.5
    };
    return m;
}

// The presets. Defaults are the core's, except the transmission probability
// of the random-mixing, group-mixing and quarantine presets (as in
// epiworldRShiny).
inline std::vector<ModelInfo> make_registry() {

    const std::map<std::string, double> mixed = {{"transmission_prob", 0.1}};
    const std::set<std::string> ignored = {"vax_reduction_recovery_rate"};
    const epiworld::ParamInfo contact_rate = {"contact_rate", "Contact rate",
        4.0, 0.0, 100.0, false, "Average number of contacts per agent per day."};

    std::vector<ModelInfo> out;
    for (std::string id : {"SIR", "SIS", "SEIR", "SIRD", "SISD", "SEIRD"})
        out.push_back(preset(id, id, "basic", "network", id));
    for (std::string id : {"SIR", "SEIR", "SIRD", "SEIRD"})
        out.push_back(preset(id + "CONN", id + " (random mixing)", "connected",
            "connected", id, mixed, {}, {contact_rate}, random_mixing));
    for (std::string id : {"SIR", "SEIR"})
        out.push_back(preset(id + "Mixing", id + " (mixing)", "mixing", "mixing",
            id, mixed, {}, {}, group_mixing));
    out.push_back(preset("SEIRMixingQuarantine", "SEIR (mixing) with quarantine",
        "mixing", "mixing", "SEIRQuarantine", mixed, {}, {}, group_mixing));
    out.push_back(preset("SEIRNetworkQuarantine", "SEIR (network) with quarantine",
        "basic", "network", "SEIRQuarantine", mixed));

    // Measles (UofUEpiBio/measles); the school model adds its own layer
    out.push_back(preset("MeaslesSchool", "Measles in a school", "measles",
        "connected", "MeaslesSchool", {}, ignored));
    out.push_back(measles_mixing(preset("MeaslesMixing", "Measles (mixing)",
        "measles", "mixing", "MeaslesMixing", {}, ignored, {}, group_mixing)));
    out.push_back(measles_mixing(preset("MeaslesMixingRiskQuarantine",
        "Measles (mixing) with risk-based quarantine", "measles", "mixing",
        "MeaslesMixingRiskQuarantine", {}, ignored, {}, group_mixing)));
    return out;
}

} // namespace detail

/// Every model epiworldjs can run, in display order.
inline const std::vector<ModelInfo> & registry() {
    static const std::vector<ModelInfo> models = detail::make_registry();
    return models;
}

inline const ModelInfo & find_model(const std::string & id) {
    for (const auto & m : registry())
        if (m.id == id)
            return m;

    std::string known;
    for (const auto & m : registry())
        known += (known.empty() ? "" : ", ") + m.id;
    throw std::invalid_argument("Unknown model \"" + id + "\". Available models: " + known + ".");
}

} // namespace epiworldjs

#endif
