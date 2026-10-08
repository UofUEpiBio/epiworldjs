// What each model should do, checked on its daily counts: no infections
// without transmission, outbreaks that only move forward, and parameters that
// switch states off (hospitalization, death, quarantine, isolation, vaccines).
import test from "node:test";
import assert from "node:assert/strict";

import { Core } from "../src/core.js";

const core = await Core.load();

// Daily counts by state, added up over a few simulations (with one initial
// case, some never spread)
function run(model, params = {}) {
  const ndays = 60;
  const t = core.run({ model: model.id, params, ndays, nsims: 4, seed: 4 }).total_hist;
  const counts = Object.fromEntries(model.states.map((s) => [s, Array(ndays + 1).fill(0)]));
  t.state.forEach((s, i) => (counts[s][t.date[i]] += t.counts[i]));
  return counts;
}

// Agents never infected (quarantine does not change that)
const uninfected = (c) =>
  c.Susceptible.map((s, d) => s + (c["Quarantined Susceptible"]?.[d] ?? 0));

const up = (x) => x.every((v, d) => d === 0 || v >= x[d - 1]);
const down = (x) => up(x.map((v) => -v));
const zero = (c, prefix) =>
  Object.keys(c).filter((s) => s.startsWith(prefix)).every((s) => c[s].every((v) => v === 0));

for (const model of core.models()) {
  const has = (name) => model.params.some((p) => p.name === name);
  const transmission = model.params.find((p) => /Transmission/.test(p.name)).name;
  // In SIS models the infected become susceptible again
  const immunity = model.states.some((s) => s === "Recovered" || s === "Removed");

  // Upstream bug: ModelSISD sets a death probability of 0.01, not "Death rate"
  const todo = model.id === "SISD" && "epiworld's SISD ignores \"Death rate\"";

  test(`${model.id} behaves as the model says`, { todo }, () => {
    // No transmission, no new infections
    assert.ok(up(uninfected(run(model, { [transmission]: 0 }))), "infections without transmission");

    // An outbreak with the defaults; with immunity, the never-infected only go
    // down and the recovered, removed and dead only go up
    const c = run(model);
    const u = uninfected(c);
    assert.ok(u.at(-1) < u[0], "no outbreak with the defaults");
    if (immunity) assert.ok(down(u), "agents became uninfected again");
    for (const s of ["Recovered", "Removed", "Deceased"])
      if (c[s]) assert.ok(up(c[s]), `${s} went down`);

    if (has("Death rate"))
      assert.ok(zero(run(model, { "Death rate": 0 }), "Deceased"), "deaths with a death rate of 0");

    if (has("Hospitalization rate")) {
      const c = run(model, { "Hospitalization rate": 0 });
      assert.ok(zero(c, "Hospitalized") && zero(c, "Detected Hospitalized"),
        "hospitalizations with a rate of 0");
    }

    // Negative periods turn quarantine and isolation off
    const off = model.params.filter((p) => /^(Quarantine|Isolation) period/.test(p.name));
    if (off.length) {
      const c = run(model, Object.fromEntries(off.map((p) => [p.name, -1])));
      assert.ok(zero(c, "Quarantined") && zero(c, "Isolated"), "quarantines or isolation while off");
    }

    // A fully effective vaccine for everyone stops the outbreak
    if (has("Vax efficacy")) {
      const v = run(model, { "Vaccination rate": 1, "Vax efficacy": 1 });
      assert.ok(up(uninfected(v)), "infections with everyone immune");
    }
  });
}
