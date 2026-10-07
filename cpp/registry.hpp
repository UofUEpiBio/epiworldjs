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

    double operator()(const std::string & name) const { return params.at(name); }
};

/// How agents are created, which decides what `population` a spec can take:
/// - "network": a small-world network or an edge list, built after the model;
/// - "connected": fully mixed, only `n`;
/// - "mixing": groups (entities) of given sizes and a contact matrix.
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

    const ParamInfo transmission_net = prob("Transmission rate", 0.05, "Probability of transmission per contact with an infected agent.");
    const ParamInfo transmission_conn = prob("Transmission rate", 0.1, "Probability of transmission per contact with an infected agent.");
    const ParamInfo recovery = prob("Recovery rate", 0.14, "Daily probability that an infected agent recovers.");
    const ParamInfo death = prob("Death rate", 0.01, "Daily probability that an infected agent dies.");
    const ParamInfo incubation = days("Incubation days", 7.0, 1.0, "Average number of days in the exposed state.");

    // The SEIR connected, mixing and quarantine models use other names
    const ParamInfo p_transmission = prob("Prob. Transmission", 0.1, "Probability of transmission per contact with an infected agent.");
    const ParamInfo p_recovery = prob("Prob. Recovery", 0.14, "Daily probability that an infected agent recovers.");
    const ParamInfo avg_incubation = days("Avg. Incubation days", 7.0, 1.0, "Average number of days in the exposed state.");

    const std::vector<ParamInfo> quarantine = {
        prob("Hospitalization rate", 0.05, "Daily probability that an infected agent is hospitalized."),
        days("Hospitalization period", 7.0, 1.0, "Average number of days in the hospital."),
        days("Days undetected", 3.0, -1.0, "Average number of days before an infected agent is detected (negative: never)."),
        days("Quarantine period", 14.0, -1.0, "Days in quarantine for the contacts of a detected case (negative: no quarantine).", true),
        prob("Quarantine willingness", 0.8, "Probability that a contact complies with quarantine."),
        prob("Isolation willingness", 0.5, "Probability that a detected case complies with isolation."),
        days("Isolation period", 7.0, -1.0, "Days in isolation for a detected case (negative: no isolation).", true),
        prob("Contact tracing success rate", 0.7, "Probability that a contact is traced."),
        {"Contact tracing days prior", 3.0, 0.0, 30.0, 1.0, true, "Days before detection whose contacts are traced."},
    };

    auto with = [](std::vector<ParamInfo> a, const std::vector<ParamInfo> & b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    };

    // Builds each model through its own (positional) constructor, so the
    // parameters reach epiworld exactly as epiworldR would pass them.
    return {
        {"SIR", "SIR", "basic", "network", {transmission_net, recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIR<>>(virus_name, a.prevalence,
                    a("Transmission rate"), a("Recovery rate"));
            }},
        {"SIS", "SIS", "basic", "network", {transmission_net, recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIS<>>(virus_name, a.prevalence,
                    a("Transmission rate"), a("Recovery rate"));
            }},
        {"SEIR", "SEIR", "basic", "network", {transmission_net, incubation, recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIR<>>(virus_name, a.prevalence,
                    a("Transmission rate"), a("Incubation days"), a("Recovery rate"));
            }},
        {"SIRD", "SIRD", "basic", "network", {transmission_net, recovery, death},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIRD<>>(virus_name, a.prevalence,
                    a("Transmission rate"), a("Recovery rate"), a("Death rate"));
            }},
        {"SISD", "SISD", "basic", "network", {transmission_net, recovery, death},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSISD<>>(virus_name, a.prevalence,
                    a("Transmission rate"), a("Recovery rate"), a("Death rate"));
            }},
        {"SEIRD", "SEIRD", "basic", "network", {transmission_net, incubation, recovery, death},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIRD<>>(virus_name, a.prevalence,
                    a("Transmission rate"), a("Incubation days"), a("Recovery rate"),
                    a("Death rate"));
            }},
        {"SIRCONN", "SIR (connected)", "connected", "connected",
            {contact_rate(), transmission_conn, recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIRCONN<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Contact rate"), a("Transmission rate"), a("Recovery rate"));
            }},
        {"SEIRCONN", "SEIR (connected)", "connected", "connected",
            {contact_rate(), p_transmission, avg_incubation, p_recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIRCONN<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Contact rate"), a("Prob. Transmission"),
                    a("Avg. Incubation days"), a("Prob. Recovery"));
            }},
        {"SIRDCONN", "SIRD (connected)", "connected", "connected",
            {contact_rate(), transmission_conn, recovery, death},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIRDCONN<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Contact rate"), a("Transmission rate"), a("Recovery rate"),
                    a("Death rate"));
            }},
        {"SEIRDCONN", "SEIRD (connected)", "connected", "connected",
            {contact_rate(), p_transmission, avg_incubation, p_recovery, death},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIRDCONN<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Contact rate"), a("Prob. Transmission"),
                    a("Avg. Incubation days"), a("Prob. Recovery"), a("Death rate"));
            }},
        {"SIRMixing", "SIR (mixing)", "mixing", "mixing", {p_transmission, p_recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSIRMixing<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Prob. Transmission"), a("Prob. Recovery"), a.contact_matrix);
            }},
        {"SEIRMixing", "SEIR (mixing)", "mixing", "mixing",
            {p_transmission, avg_incubation, p_recovery},
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIRMixing<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Prob. Transmission"), a("Avg. Incubation days"),
                    a("Prob. Recovery"), a.contact_matrix);
            }},
        {"SEIRMixingQuarantine", "SEIR (mixing) with quarantine", "mixing", "mixing",
            with({p_transmission, avg_incubation, p_recovery}, quarantine),
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIRMixingQuarantine<>>(virus_name,
                    static_cast<epiworld_fast_uint>(a.n), a.prevalence,
                    a("Prob. Transmission"), a("Avg. Incubation days"),
                    a("Prob. Recovery"), a.contact_matrix,
                    a("Hospitalization rate"), a("Hospitalization period"),
                    a("Days undetected"),
                    static_cast<epiworld_fast_int>(a("Quarantine period")),
                    a("Quarantine willingness"), a("Isolation willingness"),
                    static_cast<epiworld_fast_int>(a("Isolation period")),
                    a("Contact tracing success rate"),
                    static_cast<epiworld_fast_uint>(a("Contact tracing days prior")));
            }},
        {"SEIRNetworkQuarantine", "SEIR (network) with quarantine", "basic", "network",
            with({p_transmission, avg_incubation, p_recovery}, quarantine),
            [](const BuildArgs & a) {
                return std::make_unique<ModelSEIRNetworkQuarantine<>>(virus_name,
                    a.prevalence, a("Prob. Transmission"), a("Avg. Incubation days"),
                    a("Prob. Recovery"),
                    a("Hospitalization rate"), a("Hospitalization period"),
                    a("Days undetected"),
                    static_cast<epiworld_fast_int>(a("Quarantine period")),
                    a("Quarantine willingness"), a("Isolation willingness"),
                    static_cast<epiworld_fast_int>(a("Isolation period")),
                    a("Contact tracing success rate"),
                    static_cast<epiworld_fast_uint>(a("Contact tracing days prior")));
            }},

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
                    return std::make_unique<measles::ModelMeaslesMixing<>>(
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
                        a("Rash reduction contact rate"));
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
                    return std::make_unique<measles::ModelMeaslesMixingRiskQuarantine<>>(
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
                        static_cast<epiworld_fast_uint>(a("Contact tracing days window")));
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
