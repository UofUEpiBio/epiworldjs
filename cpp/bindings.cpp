#include <climits>
#include <cmath>
#include <set>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "core.hpp"

using emscripten::val;

namespace {

using epiworldjs::detail::require;

std::string type_of(const val & v) {
    return v.typeOf().as<std::string>();
}

bool is_array(const val & v) {
    return val::global("Array").call<bool>("isArray", v);
}

double to_number(const val & v, const std::string & name) {
    require(type_of(v) == "number", name + " must be a number.");
    return v.as<double>();
}

// JS numbers arrive as doubles; this rejects NaN, fractions, and values that
// would wrap around when converted to int.
int to_int(const val & v, const std::string & name) {
    double value = to_number(v, name);
    require(value >= INT_MIN && value <= INT_MAX && std::floor(value) == value,
        name + " must be an integer.");
    return static_cast<int>(value);
}

std::string as_string(const val & v, const std::string & name) {
    require(type_of(v) == "string", name + " must be a string.");
    return v.as<std::string>();
}

// Accepts an array or a typed array; nested arrays (a matrix given as rows)
// are flattened row by row.
template<typename T, typename Convert>
std::vector<T> to_vector(const val & v, const std::string & name, Convert convert) {
    require(is_array(v) || val::global("ArrayBuffer").call<bool>("isView", v),
        name + " must be an array.");
    std::vector<T> out;
    const unsigned length = v["length"].as<unsigned>();
    for (unsigned i = 0; i < length; ++i) {
        val item = v[i];
        if (is_array(item)) {
            auto row = to_vector<T>(item, name, convert);
            out.insert(out.end(), row.begin(), row.end());
        } else {
            out.push_back(convert(item, name + "[" + std::to_string(i) + "]"));
        }
    }
    return out;
}

std::vector<std::string> keys(const val & object) {
    return emscripten::vecFromJSArray<std::string>(val::global("Object").call<val>("keys", object));
}

// Typos should fail loudly rather than be ignored
void require_known_keys(const val & object, const std::set<std::string> & known, const std::string & what) {
    for (const auto & key : keys(object))
        if (!known.count(key)) {
            std::string names;
            for (const auto & k : known)
                names += (names.empty() ? "" : ", ") + k;
            throw std::invalid_argument("Unknown " + what + " field \"" + key + "\". Known fields: " + names + ".");
        }
}

bool has(const val & object, const char * key) {
    return !object[key].isUndefined();
}

epiworldjs::Population to_population(const val & v) {
    require(type_of(v) == "object" && !v.isNull(), "population must be an object.");
    require_known_keys(v, {"type", "n", "k", "p", "directed", "source", "target",
        "sizes", "contact_matrix"}, "population");

    epiworldjs::Population pop;
    if (has(v, "type")) pop.type = as_string(v["type"], "population.type");
    if (has(v, "n")) pop.n = to_int(v["n"], "population.n");
    if (has(v, "k")) pop.k = to_int(v["k"], "population.k");
    if (has(v, "p")) pop.p = to_number(v["p"], "population.p");
    if (has(v, "directed")) {
        require(type_of(v["directed"]) == "boolean", "population.directed must be true or false.");
        pop.directed = v["directed"].as<bool>();
    }
    if (has(v, "source")) pop.source = to_vector<int>(v["source"], "population.source", to_int);
    if (has(v, "target")) pop.target = to_vector<int>(v["target"], "population.target", to_int);
    if (has(v, "sizes")) pop.sizes = to_vector<int>(v["sizes"], "population.sizes", to_int);
    if (has(v, "contact_matrix"))
        pop.contact_matrix = to_vector<double>(v["contact_matrix"], "population.contact_matrix", to_number);
    return pop;
}

epiworldjs::RunSpec to_spec(const val & v) {
    require(type_of(v) == "object" && !v.isNull(), "The spec must be an object.");
    require_known_keys(v, {"model", "params", "prevalence", "ndays", "nsims", "seed",
        "n", "population", "outputs"}, "spec");
    require(has(v, "model"), "The spec needs a model.");

    epiworldjs::RunSpec spec;
    spec.model = as_string(v["model"], "model");
    if (has(v, "params")) {
        val params = v["params"];
        require(type_of(params) == "object" && !params.isNull(), "params must be an object.");
        for (const auto & name : keys(params))
            spec.params[name] = to_number(params[name], "\"" + name + "\"");
    }
    if (has(v, "prevalence")) spec.prevalence = to_number(v["prevalence"], "prevalence");
    if (has(v, "ndays")) spec.ndays = to_int(v["ndays"], "ndays");
    if (has(v, "nsims")) spec.nsims = to_int(v["nsims"], "nsims");
    if (has(v, "seed")) spec.seed = to_int(v["seed"], "seed");
    if (has(v, "population")) spec.population = to_population(v["population"]);
    if (has(v, "n")) {
        require(!has(v, "population") || !has(v["population"], "n"),
            "Give n either at the top level or in population, not both.");
        spec.population.n = to_int(v["n"], "n");
    }
    if (has(v, "outputs")) spec.outputs = to_vector<std::string>(v["outputs"], "outputs", as_string);
    return spec;
}

// Copies into arrays that JS owns, so callers have nothing to free.
val to_js(const std::vector<int> & values) {
    return val::global("Int32Array").new_(emscripten::typed_memory_view(values.size(), values.data()));
}

val to_js(const std::vector<double> & values) {
    return val::global("Float64Array").new_(emscripten::typed_memory_view(values.size(), values.data()));
}

val to_js(const std::vector<std::string> & values) {
    val array = val::array();
    for (const auto & value : values)
        array.call<void>("push", value);
    return array;
}

// Turns C++ exceptions into JS Errors carrying the same message.
template<typename Fun>
val js_errors(Fun fun) {
    try {
        return fun();
    } catch (const std::exception & e) {
        val::global("Error").new_(std::string(e.what())).throw_();
    }
}

/// Runs `spec`; `simIds` (optional) selects which of the `nsims` simulations.
/// Returns {output name: {column name: Int32Array | Float64Array | string[]}}.
val run(val spec, val sim_ids) {
    return js_errors([&] {
        std::vector<int> ids;
        if (!sim_ids.isUndefined() && !sim_ids.isNull())
            ids = to_vector<int>(sim_ids, "simIds", to_int);

        val out = val::object();
        for (const auto & [name, table] : epiworldjs::run_many(to_spec(spec), ids)) {
            val columns = val::object();
            for (size_t j = 0; j < table.columns.size(); ++j)
                columns.set(table.colnames[j],
                    std::visit([](const auto & c) { return to_js(c); }, table.columns[j]));
            out.set(name, columns);
        }
        return out;
    });
}

val list_models() {
    return js_errors([] {
        val models = val::array();
        for (const auto & info : epiworldjs::registry()) {
            val params = val::array();
            for (const auto & p : info.params) {
                val param = val::object();
                param.set("name", p.name);
                param.set("value", p.value);
                param.set("min", p.min);
                param.set("max", p.max);
                param.set("step", p.step);
                param.set("integer", p.integer);
                param.set("description", p.description);
                params.call<void>("push", param);
            }
            val model = val::object();
            model.set("id", info.id);
            model.set("label", info.label);
            model.set("family", info.family);
            model.set("population", info.population);
            model.set("states", to_js(epiworldjs::state_labels(info)));
            model.set("params", params);
            models.call<void>("push", model);
        }
        return models;
    });
}

val output_names() {
    return to_js(epiworld::run_output_names());
}

val version() {
    val out = val::object();
    out.set("epiworld", std::to_string(EPIWORLD_VERSION_MAJOR) + "." +
        std::to_string(EPIWORLD_VERSION_MINOR) + "." + std::to_string(EPIWORLD_VERSION_PATCH) +
        EPIWORLD_VERSION_PRERELEASE);
    out.set("measles", measles_version());
    return out;
}

} // namespace

EMSCRIPTEN_BINDINGS(epiworldjs) {
    emscripten::function("run", &run);
    emscripten::function("listModels", &list_models);
    emscripten::function("outputNames", &output_names);
    emscripten::function("version", &version);
}
