import test from "node:test";
import assert from "node:assert/strict";

import { Core } from "../src/core.js";
import { Epiworld } from "../src/index.js";

const core = await Core.load();

test("models() lists every model with its parameters and states", () => {
  const models = core.models();
  assert.deepEqual(
    models.map((m) => m.id),
    [
      "SIR", "SIS", "SEIR", "SIRD", "SISD", "SEIRD",
      "SIRCONN", "SEIRCONN", "SIRDCONN", "SEIRDCONN",
      "SIRMixing", "SEIRMixing", "SEIRMixingQuarantine", "SEIRNetworkQuarantine",
      "MeaslesSchool", "MeaslesMixing", "MeaslesMixingRiskQuarantine",
    ],
  );

  const seirconn = models.find((m) => m.id === "SEIRCONN");
  assert.deepEqual(seirconn.states, ["Susceptible", "Exposed", "Infected", "Recovered"]);
  assert.deepEqual(seirconn.params[0], {
    name: "Contact rate", value: 4, min: 0, max: 100, step: 0.1, integer: false,
    description: "Average number of contacts per agent per day.",
  });
});

for (const model of core.models()) {
  test(`${model.id} runs with its defaults and conserves the population`, () => {
    const n = 500, ndays = 20, nsims = 2;
    const { total_hist: t } = core.run({ model: model.id, n, ndays, nsims });

    assert.deepEqual(Object.keys(t), ["sim_id", "date", "nviruses", "state", "counts"]);
    assert.ok(t.counts instanceof Int32Array);
    const k = model.states.length;
    assert.equal(t.counts.length, nsims * (ndays + 1) * k);

    for (let i = 0; i < t.counts.length; i += k) {
      assert.deepEqual(t.state.slice(i, i + k), model.states);
      let total = 0;
      for (let s = 0; s < k; s++) total += t.counts[i + s];
      assert.equal(total, n, `day ${t.date[i]} of sim ${t.sim_id[i]}`);
    }
  });
}

test("simulations run in slices match the full run", () => {
  const spec = {
    model: "SEIRCONN", n: 1000, ndays: 30, nsims: 6, seed: 5,
    outputs: ["total_hist", "transition"],
  };
  const full = core.run(spec);
  const slices = { even: [0, 2, 4], odd: [1, 3, 5] };

  for (const sims of Object.values(slices)) {
    const part = core.run(spec, sims);
    for (const [name, table] of Object.entries(full)) {
      const rows = (_, i) => sims.includes(table.sim_id[i]);
      for (const [column, values] of Object.entries(table))
        assert.deepEqual(Array.from(part[name][column]), Array.from(values).filter(rows), `${name}.${column}`);
    }
  }
});

test("the same seed gives the same run; another seed does not", () => {
  const spec = { model: "SIR", n: 1000, ndays: 30, seed: 3 };
  assert.deepEqual(core.run(spec).total_hist.counts, core.run(spec).total_hist.counts);
  assert.notDeepEqual(core.run(spec).total_hist.counts, core.run({ ...spec, seed: 4 }).total_hist.counts);
});

test("every output can be requested, with double columns as Float64Array", () => {
  const out = core.run({ model: "SEIRCONN", n: 500, ndays: 20, outputs: core.outputNames() });
  assert.deepEqual(Object.keys(out).sort(), [...core.outputNames()].sort());
  assert.ok(out.reproductive.rt instanceof Int32Array);
  assert.ok(out.hospitalizations.weight instanceof Float64Array);
});

test("populations: edge list and groups", () => {
  const n = 100;
  const source = Array.from({ length: n }, (_, i) => i);
  const target = source.map((i) => (i + 1) % n);
  const ring = core.run({ model: "SIR", population: { type: "edgelist", n, source, target } });
  assert.equal(ring.total_hist.counts.slice(0, 3).reduce((a, b) => a + b), n);

  const groups = core.run({
    model: "SIRMixing",
    population: { type: "groups", sizes: [60, 40], contact_matrix: [[5, 1], [1, 5]] },
  });
  assert.equal(groups.total_hist.counts.slice(0, 3).reduce((a, b) => a + b), 100);
});

test("invalid specs throw an Error naming the problem", () => {
  const ok = { model: "SIRCONN", n: 100 };
  const fails = (spec, message) => assert.throws(() => core.run(spec), message);

  fails({ ...ok, model: "SIRX" }, /Unknown model "SIRX"/);
  fails({ ...ok, params: { "Contact Rate": 2 } }, /Unknown parameter "Contact Rate" for model SIRCONN/);
  fails({ ...ok, params: { "Transmission rate": 2 } }, /"Transmission rate" must be between 0 and 1/);
  fails({ ...ok, params: { "Transmission rate": "0.1" } }, /"Transmission rate" must be a number/);
  fails({ ...ok, ndays: 1.5 }, /ndays must be an integer/);
  fails({ ...ok, seed: 3e9 }, /seed must be an integer/);
  fails({ ...ok, seed: -1 }, /seed must be greater/);
  fails({ ...ok, nday: 10 }, /Unknown spec field "nday"/);
  fails({ ...ok, outputs: ["total"] }, /Unknown output "total"/);
  fails({ ...ok, population: { n: 5 } }, /either at the top level or in population/);
  fails({ model: "SIRMixing", population: { sizes: [1, 2], contact_matrix: [1] } }, /contact_matrix must be 2 x 2/);
  assert.throws(() => core.run(ok, [3]), /Simulation ids/);
});

test("measles models use their own defaults", () => {
  const school = core.run({ model: "MeaslesSchool", ndays: 0 }).total_hist;
  const total = school.counts.reduce((a, b) => a + b);
  assert.equal(total, 500);

  // One initial case: the agents outside "Susceptible" on day 0
  const susceptible = school.counts[school.state.indexOf("Susceptible")];
  assert.ok(total - susceptible >= 1);

  const mixing = core.run({ model: "MeaslesMixing", ndays: 0 }).total_hist;
  assert.equal(mixing.counts.reduce((a, b) => a + b), 9000);
});

test("version() reports epiworld and measles", () => {
  assert.match(core.version().epiworld, /^\d+\.\d+\.\d+/);
  assert.match(core.version().measles, /^\d+\.\d+\.\d+/);
});

test("Epiworld.run resolves to the same tables", async () => {
  const ew = await Epiworld.load();
  const spec = { model: "SIS", n: 200, ndays: 10, seed: 9 };
  assert.deepEqual((await ew.run(spec)).tables, core.run(spec));
  assert.equal(ew.models().length, 17);
  ew.terminate();
});
