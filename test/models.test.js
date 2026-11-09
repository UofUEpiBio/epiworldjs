// What each model should do, checked on its daily counts: no infections
// without transmission, outbreaks that only move forward, and parameters that
// switch stages and statuses off (hospitalization, death, quarantine,
// isolation, vaccines).
import test from "node:test";
import assert from "node:assert/strict";

import { Core } from "../src/core.js";

const core = await Core.load();

// Daily counts by stage and by status, added up over a few simulations (with
// one initial case, some never spread)
function run(model, params = {}) {
  const ndays = 60;
  const t = core.run({ model: model.id, params, ndays, nsims: 4, seed: 4 }).total_hist;
  const by = (labels, column) => {
    const counts = Object.fromEntries(labels.map((s) => [s, Array(ndays + 1).fill(0)]));
    column.forEach((s, i) => (counts[s][t.date[i]] += t.counts[i]));
    return counts;
  };
  return { ...by(model.stages, t.stage), status: by(model.statuses, t.status) };
}

// Agents never infected (whatever their status)
const uninfected = (c) => c.Susceptible;

const up = (x) => x.every((v, d) => d === 0 || v >= x[d - 1]);
const down = (x) => up(x.map((v) => -v));
const zero = (c, status) => (c.status[status] ?? []).every((v) => v === 0);

for (const model of core.models()) {
  const has = (arg) => model.params.some((p) => p.arg === arg);
  const transmission = model.params.find((p) => /^transmission_/.test(p.arg)).arg;
  // In SIS models the infected become susceptible again
  const immunity = model.stages.some((s) => s === "Recovered" || s === "Removed");

  test(`${model.id} behaves as the model says`, () => {
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

    if (has("hospitalization_rate")) {
      const c = run(model, { hospitalization_rate: 0 });
      assert.ok(zero(c, "Hospitalized"), "hospitalizations with a rate of 0");
    }

    // Negative periods turn quarantine and isolation off
    const off = model.params.filter((p) => /^(quarantine|isolation)_period/.test(p.arg));
    if (off.length) {
      const c = run(model, Object.fromEntries(off.map((p) => [p.arg, -1])));
      assert.ok(zero(c, "Quarantined") && zero(c, "Isolated"), "quarantines or isolation while off");
    }

    // A fully effective vaccine for everyone stops the outbreak
    if (has("vax_efficacy")) {
      const v = run(model, { prop_vaccinated: 1, vax_efficacy: 1 });
      assert.ok(up(uninfected(v)), "infections with everyone immune");
    }
  });

  if (has("death_rate"))
    test(`${model.id} has no deaths with a death rate of 0`, () => {
      assert.ok(run(model, { death_rate: 0 }).Deceased.every((v) => v === 0));
    });
}
