import test from "node:test";
import assert from "node:assert/strict";

import { Epiworld } from "../src/index.js";

const spec = { n: 100, prevalence: 0.05, ndays: 10, seed: 42 };

test("runSIRCONN returns one row per day and state, as JS arrays", async () => {
  const ew = await Epiworld.load();
  const { day, state, count } = ew.runSIRCONN(spec);

  assert.ok(day instanceof Int32Array);
  assert.ok(count instanceof Int32Array);
  assert.ok(Array.isArray(state));
  assert.equal(day.length, 3 * (spec.ndays + 1));
  assert.deepEqual(state.slice(0, 3), ["Susceptible", "Infected", "Recovered"]);

  for (let i = 0; i < count.length; i += 3)
    assert.equal(count[i] + count[i + 1] + count[i + 2], spec.n);
});

test("the same seed gives the same run", async () => {
  const ew = await Epiworld.load();
  assert.deepEqual(ew.runSIRCONN(spec).count, ew.runSIRCONN(spec).count);
});

test("load() instantiates the module once", async () => {
  const [a, b] = await Promise.all([Epiworld.load(), Epiworld.load()]);
  assert.equal(a.module, b.module);
});

test("invalid arguments throw an Error with epiworld's message", async () => {
  const ew = await Epiworld.load();
  assert.throws(() => ew.runSIRCONN({ ...spec, n: 0 }), /n must be greater than 0/);
  assert.throws(() => ew.runSIRCONN({ ...spec, prevalence: undefined }), /prevalence/);
  assert.throws(() => ew.runSIRCONN({ ...spec, seed: 3e9 }), /seed must be an integer/);
  assert.throws(() => ew.runSIRCONN({ ...spec, seed: -1 }), /seed must be greater/);
});
