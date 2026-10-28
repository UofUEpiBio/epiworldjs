#ifndef EPIWORLDJS_REGISTRY_HPP
#define EPIWORLDJS_REGISTRY_HPP

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "epiworld.hpp"
#include "measles/measles.hpp"

namespace epiworldjs {

using Model = epiworld::Model<>;

/// One model parameter. `name` is epiworld's own name for it (what
/// `get_param()` and epiworldR use); `min`/`max` are hard limits, enforced
/// before the model is built, and also what a slider should span.
struct ParamInfo {
    std::string name;
    double value;
    double min;
    double max;
    double step;
    bool integer;
    std::string description;
};

/// What a builder receives: the parameters (complete, validated) and the
/// population-level arguments that epiworld's constructors take.
struct BuildArgs {
    std::map<std::string, double> params;
    double prevalence;
    int n;
    int n_infected; ///< round(prevalence * n), for constructors that take a count
    std::vector<double> contact_matrix; ///< Column-major, as epiworld expects
    std::vector<int> sizes; ///< Group sizes ("mixing" population), in agent order

    double operator()(const std::string & name) const { return params.at(name); }
};

/// How agents meet, which decides what `population` a spec can take:
/// - "network": a small-world network or an edge list, added after the model;
/// - "connected": random mixing, only `n` (the builder adds the layer);
/// - "mixing": groups of given sizes and a contact matrix (the builder adds
///   the layer, or the entities for the measles models).
struct ModelInfo {
    std::string id;
    std::string label;
    std::string family;
    std::string population;
    std::vector<ParamInfo> params;
    std::function<std::unique_ptr<Model>(const BuildArgs &)> build;
    /// Defaults for the spec fields that are not parameters
    int n = 10000;
    double prevalence = 0.01;
    /// Mixing models: daily contacts between three equal groups, row-major
    std::vector<double> contact_matrix = {
        18.0, 1.0, 1.0,
         2.0, 16.0, 2.0,
         2.0, 4.0, 14.0
    };
};

namespace detail {

using namespace epiworld::epimodels;

// Shorthands for the parameter kinds that repeat across models
inline ParamInfo prob(std::string name, double value, std::string description) {
    return {std::move(name), value, 0.0, 1.0, 0.01, false, std::move(description)};
}

inline ParamInfo days(std::string name, double value, double min, std::string description, bool integer = false) {
    return {std::move(name), value, min, 60.0, integer ? 1.0 : 0.5, integer, std::move(description)};
}

inline ParamInfo contact_rate() {
    return {"Contact rate", 4.0, 0.0, 100.0, 0.1, false, "Average number of contacts per agent per day."};
}

const char * const virus_name = "Disease";

// Random mixing: everyone in one group, meeting "Contact rate" others a day
inline std::unique_ptr<Model> random_mixing(std::unique_ptr<Model> model, const BuildArgs & a) {
    model->add_param(a("Contact rate"), "Contact rate");
    auto mix = epiworld::layer::mixing<>("mixing");
    mix->set_groups(std::vector<size_t>(static_cast<size_t>(a.n), 0u), 1u);
    mix->set_contact_matrix({1.0});
    mix->set_contact_rate("Contact rate");
    model->add_layer(mix);
    return model;
}

// Groups of consecutive agents mixing by the contact matrix
inline std::unique_ptr<Model> group_mixing(std::unique_ptr<Model> model, const BuildArgs & a) {
    std::vector<size_t> groups;
    for (size_t g = 0; g < a.sizes.size(); ++g)
        groups.insert(groups.end(), static_cast<size_t>(a.sizes[g]), g);
    auto mix = epiworld::layer::mixing<>("mixing");
    mix->set_groups(std::move(groups), a.sizes.size());
    mix->set_contact_matrix(a.contact_matrix);
    model->add_layer(mix);
    return model;
}

// The quarantine presets: ModelSEIRQuarantine, on a mixing layer or a network
inline std::unique_ptr<Model> seir_quarantine(const BuildArgs & a) {
    using namespace epiworld::epimodels;
    return std::make_unique<ModelSEIRQuarantine<>>(virus_name,
        static_cast<epiworld_fast_uint>(a.n), a.prevalence,
        a("Transmission probability"), a("Latent period"), a("Recovery rate"),
        a("Hospitalization rate"), a("Hospitalization period"),
        a("Detection delay"),
        static_cast<epiworld_fast_int>(a("Quarantine period")),
        a("Quarantine willingness"), a("Isolation willingness"),
        static_cast<epiworld_fast_int>(a("Isolation period")),
        a("Contact tracing probability"),
        static_cast<epiworld_fast_uint>(a("Contact tracing window")));
}

// The measles mixing models still take their groups from entities (until the
// measles models move to layers)
template<typename M>
inline std::unique_ptr<Model> with_entities(std::unique_ptr<M> model, const BuildArgs & a) {
    int from = 0;
    for (size_t i = 0; i < a.sizes.size(); ++i) {
        model->add_entity(epiworld::Entity<>("Group " + std::to_string(i + 1),
            epiworld::distribute_entity_to_range<>(from, from + a.sizes[i])));
        from += a.sizes[i];
    }
    return model;
}

// The disease and policy parameters the measles models share. `mixing` adds
// the isolation willingness, which the school model does not have.
inline std::vector<ParamInfo> measles_disease_and_policy(bool mixing) {
    std::vector<ParamInfo> out = {
        prob("Vax efficacy", 0.97, "Probability that the vaccine prevents infection."),
        prob("Vaccination rate", 1.0 - 1.0 / 15.0, "Proportion of agents vaccinated."),
        days("Incubation period", 12.0, 1.0, "Average number of days from exposure to the prodromal stage."),
        days("Prodromal period", 4.0, 1.0, "Average number of days infectious before the rash."),
        days("Rash period", 3.0, 1.0, "Average number of days with rash."),
        prob("Hospitalization rate", 0.2, "Probability that an agent with rash is hospitalized."),
        days("Hospitalization period", 7.0, 1.0, "Average number of days in the hospital."),
        days("Days undetected", 2.0, 0.0, "Average number of days before a case with rash is detected."),
        days("Quarantine period", 21.0, -1.0, "Days in quarantine for the contacts of a detected case (negative: no quarantine).", true),
        prob("Quarantine willingness", 1.0, "Probability that a contact complies with quarantine."),
        days("Isolation period", 4.0, -1.0, "Days in isolation for a detected case (negative: no isolation).", true),
    };
    if (mixing)
        out.push_back(prob("Isolation willingness", 1.0, "Probability that a detected case complies with isolation."));
    return out;
}

// One initial case among three groups of 3000, 15 contacts per day (the
// examples of the measles R package)
inline void measles_mixing_defaults(ModelInfo & m) {
    m.n = 9000;
    m.prevalence = 1.0 / 9000.0;
    m.contact_matrix = {
        13.5, 0.75, 0.75,
         1.5, 12.0,  1.5,
         1.5,  3.0, 10.5
    };
}

// Defaults follow epiworldRShiny and the epiworldR examples.
inline std::vector<ModelInfo> make_registry() {

    const ParamInfo transmission_net = prob("Transmission probability", 0.05, "Probability that a contact with an infected agent infects.");
    const ParamInfo transmission_conn = prob("Transmission probability", 0.1, "Probability that a contact with an infected agent infects.");
    const ParamInfo recovery = prob("Recovery rate", 0.14, "Daily probability that an infected agent recovers.");
    const ParamInfo death = prob("Death rate", 0.01, "Daily probability that an infected agent dies.");
    const ParamInfo latent = days("Latent period", 7.0, 1.0, "Mean days in the exposed state (infected, not yet infectious).");

    const std::vector<ParamInfo> quarantine = {
        prob("Hospitalization rate", 0.05, "Daily probability that an infected agent is hospitalized."),
        days("Hospitalization period", 7.0, 1.0, "Average number of days in the hospital."),
        days("Detection delay", 3.0, -1.0, "Mean days before an infected agent is detected (0: at once; negative: never)."),
        days("Quarantine period", 14.0, -1.0, "Days in quarantine for the contacts of a detected case (negative: no quarantine).", true),
        prob("Quarantine willingness", 0.8, "Probability that a contact complies with quarantine."),
        prob("Isolation willingness", 0.5, "Probability that a detected case complies with isolation."),
        days("Isolation period", 7.0, -1.0, "Days in isolation for a detected case (negative: no isolation).", true),
        prob("Contact tracing probability", 0.7, "Probability that a contact is traced."),
        {"Contact tracing window", 3.0, 0.0, 30.0, 1.0, true, "Days before detection whose contacts are traced."},
    };

    auto with = [](std::vector<ParamInfo> a, const std::vector<ParamInfo> & b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    };

    // Builds each model through its own (positional) constructor, so the
    // parameters reach epiworld exactly as epiworldR would pass them; the
    // "connected" and "mixing" presets then add their layer.
    return {
        {"SIR", "SIR", "basic", "network", {transmission_net, recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIR<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Recovery rate"));
            }},
        {"SIS", "SIS", "basic", "network", {transmission_net, recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIS<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Recovery rate"));
            }},
        {"SEIR", "SEIR", "basic", "network", {transmission_net, latent, recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIR<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Latent period"), a("Recovery rate"));
            }},
        {"SIRD", "SIRD", "basic", "network", {transmission_net, recovery, death},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIRD<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Recovery rate"), a("Death rate"));
            }},
        {"SISD", "SISD", "basic", "network", {transmission_net, recovery, death},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSISD<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Recovery rate"), a("Death rate"));
            }},
        {"SEIRD", "SEIRD", "basic", "network", {transmission_net, latent, recovery, death},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIRD<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Latent period"), a("Recovery rate"),
                    a("Death rate"));
            }},
        {"SIRCONN", "SIR (random mixing)", "connected", "connected",
            {contact_rate(), transmission_conn, recovery},
            [](const BuildArgs & a) {
                return random_mixing(std::make_unique<ModelSIR<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Recovery rate")), a);
            }},
        {"SEIRCONN", "SEIR (random mixing)", "connected", "connected",
            {contact_rate(), transmission_conn, latent, recovery},
            [](const BuildArgs & a) {
                return random_mixing(std::make_unique<ModelSEIR<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Latent period"),
                    a("Recovery rate")), a);
            }},
        {"SIRDCONN", "SIRD (random mixing)", "connected", "connected",
            {contact_rate(), transmission_conn, recovery, death},
            [](const BuildArgs & a) {
                return random_mixing(std::make_unique<ModelSIRD<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Recovery rate"),
                    a("Death rate")), a);
            }},
        {"SEIRDCONN", "SEIRD (random mixing)", "connected", "connected",
            {contact_rate(), transmission_conn, latent, recovery, death},
            [](const BuildArgs & a) {
                return random_mixing(std::make_unique<ModelSEIRD<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Latent period"),
                    a("Recovery rate"), a("Death rate")), a);
            }},
        {"SIRMixing", "SIR (mixing)", "mixing", "mixing", {transmission_conn, recovery},
            [](const BuildArgs & a) {
                return group_mixing(std::make_unique<ModelSIR<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Recovery rate")), a);
            }},
        {"SEIRMixing", "SEIR (mixing)", "mixing", "mixing",
            {transmission_conn, latent, recovery},
            [](const BuildArgs & a) {
                return group_mixing(std::make_unique<ModelSEIR<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Transmission probability"), a("Latent period"),
                    a("Recovery rate")), a);
            }},
        {"SEIRMixingQuarantine", "SEIR (mixing) with quarantine", "mixing", "mixing",
            with({transmission_conn, latent, recovery}, quarantine),
            [](const BuildArgs & a) { return group_mixing(seir_quarantine(a), a); }},
        {"SEIRNetworkQuarantine", "SEIR (network) with quarantine", "basic", "network",
            with({transmission_conn, latent, recovery}, quarantine),
            [](const BuildArgs & a) { return seir_quarantine(a); }},

        // Measles (UofUEpiBio/measles); defaults follow its R package
        [&] {
            ModelInfo m{"MeaslesSchool", "Measles in a school", "measles", "connected",
                with({
                    {"Contact rate", 15.0 / 0.9 / 4.0, 0.0, 100.0, 0.1, false, "Average number of contacts per agent per day."},
                    prob("Transmission rate", 0.9, "Probability of transmission per contact with an infectious agent."),
                }, measles_disease_and_policy(false)),
                [](const BuildArgs & a) {
                    return std::make_unique<measles::ModelMeaslesSchool<>>(
                        static_cast<epiworld_fast_uint>(a.n),
                        static_cast<epiworld_fast_uint>(a.n_infected),
                        a("Contact rate"), a("Transmission rate"), a("Vax efficacy"),
                        0.5, // vax_reduction_recovery_rate, ignored by the model
                        a("Incubation period"), a("Prodromal period"), a("Rash period"),
                        a("Days undetected"), a("Hospitalization rate"),
                        a("Hospitalization period"), a("Vaccination rate"),
                        static_cast<epiworld_fast_int>(a("Quarantine period")),
                        a("Quarantine willingness"),
                        static_cast<epiworld_fast_int>(a("Isolation period")));
                }};
            // One initial case in a school of 500
            m.n = 500;
            m.prevalence = 0.002;
            return m;
        }(),
        [&] {
            ModelInfo m{"MeaslesMixing", "Measles (mixing)", "measles", "mixing",
                with(with({prob("Transmission rate", 0.9, "Probability of transmission per contact with an infectious agent.")},
                    measles_disease_and_policy(true)), {
                    prob("Contact tracing success rate", 1.0, "Probability that a contact is traced."),
                    {"Contact tracing days window", 4.0, 0.0, 30.0, 1.0, true, "Days before detection whose contacts are traced."},
                    prob("Rash reduction contact rate", 1.0, "Multiplier of the contact rate of agents with rash (1: no reduction)."),
                }),
                [](const BuildArgs & a) {
                    return with_entities(std::make_unique<measles::ModelMeaslesMixing<>>(
                        static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                        a("Transmission rate"), a("Vax efficacy"),
                        0.5, // vax_reduction_recovery_rate, ignored by the model
                        a("Incubation period"), a("Prodromal period"), a("Rash period"),
                        a.contact_matrix,
                        a("Hospitalization rate"), a("Hospitalization period"),
                        a("Days undetected"),
                        static_cast<epiworld_fast_int>(a("Quarantine period")),
                        a("Quarantine willingness"), a("Isolation willingness"),
                        static_cast<epiworld_fast_int>(a("Isolation period")),
                        a("Vaccination rate"), a("Contact tracing success rate"),
                        static_cast<epiworld_fast_uint>(a("Contact tracing days window")),
                        a("Rash reduction contact rate")), a);
                }};
            measles_mixing_defaults(m);
            return m;
        }(),
        [&] {
            std::vector<ParamInfo> params = with(
                {prob("Transmission rate", 0.9, "Probability of transmission per contact with an infectious agent.")},
                measles_disease_and_policy(true));
            // Quarantine depends on the contact's risk instead of being one period
            params.erase(std::remove_if(params.begin(), params.end(),
                [](const ParamInfo & p) { return p.name == "Quarantine period"; }), params.end());
            ModelInfo m{"MeaslesMixingRiskQuarantine", "Measles (mixing) with risk-based quarantine", "measles", "mixing",
                with(params, {
                    days("Quarantine period high", 21.0, -1.0, "Days in quarantine for high-risk contacts (negative: no quarantine).", true),
                    days("Quarantine period medium", 14.0, -1.0, "Days in quarantine for medium-risk contacts (negative: no quarantine).", true),
                    days("Quarantine period low", 7.0, -1.0, "Days in quarantine for low-risk contacts (negative: no quarantine).", true),
                    prob("Detection rate quarantine", 0.5, "Daily probability that an infected agent in quarantine is detected."),
                    prob("Contact tracing success rate", 1.0, "Probability that a contact is traced."),
                    {"Contact tracing days window", 4.0, 0.0, 30.0, 1.0, true, "Days before detection whose contacts are traced."},
                }),
                [](const BuildArgs & a) {
                    return with_entities(std::make_unique<measles::ModelMeaslesMixingRiskQuarantine<>>(
                        static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                        a("Transmission rate"), a("Vax efficacy"),
                        a("Incubation period"), a("Prodromal period"), a("Rash period"),
                        a.contact_matrix,
                        a("Hospitalization rate"), a("Hospitalization period"),
                        a("Days undetected"),
                        static_cast<epiworld_fast_int>(a("Quarantine period high")),
                        static_cast<epiworld_fast_int>(a("Quarantine period medium")),
                        static_cast<epiworld_fast_int>(a("Quarantine period low")),
                        a("Quarantine willingness"), a("Isolation willingness"),
                        static_cast<epiworld_fast_int>(a("Isolation period")),
                        a("Vaccination rate"), a("Detection rate quarantine"),
                        a("Contact tracing success rate"),
                        static_cast<epiworld_fast_uint>(a("Contact tracing days window"))), a);
                }};
            measles_mixing_defaults(m);
            return m;
        }(),
    };
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
