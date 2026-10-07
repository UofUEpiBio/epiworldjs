import test from "node:test";
import assert from "node:assert/strict";

import { Core, Epiworld, Result } from "../src/index.js";

const core = await Core.load();

const spec = {
  model: "SEIRCONN", n: 2000, ndays: 40, nsims: 7, seed: 11,
  outputs: ["total_hist", "transition", "transmission"],
};

test("results are identical for 0, 1, 2 and 4 workers, and match the core", async () => {
  const expected = core.run(spec);
  for (const workers of [0, 1, 2, 4]) {
    const ew = await Epiworld.load({ workers });
    try {
      assert.equal(ew.workers, workers);
      const res = await ew.run(spec);
      assert.ok(res instanceof Result);
      assert.deepEqual(res.tables, expected, `${workers} workers`);
    } finally {
      ew.terminate();
    }
  }
});

test("more workers than simulations, and concurrent runs", async () => {
  const ew = await Epiworld.load({ workers: 4 });
  try {
    const small = { model: "SIR", n: 500, ndays: 10, nsims: 2, seed: 2 };
    const [a, b] = await Promise.all([ew.run(small), ew.run({ ...small, seed: 3 })]);
    assert.deepEqual(a.tables, core.run(small));
    assert.deepEqual(b.tables, core.run({ ...small, seed: 3 }));
  } finally {
    ew.terminate();
  }
});

test("errors in workers reject with the core's message", async () => {
  const ew = await Epiworld.load({ workers: 2 });
  try {
    await assert.rejects(ew.run({ ...spec, params: { "Prob. Transmission": 2 } }),
      /"Prob. Transmission" must be between 0 and 1/);
    await assert.rejects(ew.run({ model: "SIRX" }), /Unknown model "SIRX"/);
    await assert.rejects(ew.run({ ...spec, nsims: 0 }), /nsims must be at least 1/);
    // The pool still works afterwards
    assert.equal((await ew.run({ ...spec, nsims: 2 })).nsims, 2);
  } finally {
    ew.terminate();
  }
});

test("summary() gives the median and a 95% band per state and day", async () => {
  const ew = await Epiworld.load({ workers: 2 });
  try {
    const res = await ew.run(spec);
    const s = res.summary();
    assert.deepEqual(s.states, ["Susceptible", "Exposed", "Infected", "Recovered"]);
    assert.equal(s.dates.length, spec.ndays + 1);

    // Checked against the per-simulation counts of one state and day
    const t = res.tables.total_hist;
    const day = 20, state = "Infected";
    const values = [];
    for (let i = 0; i < t.counts.length; i++)
      if (t.date[i] === day && t.state[i] === state) values.push(t.counts[i]);
    values.sort((a, b) => a - b);
    assert.equal(values.length, spec.nsims);
    assert.equal(s.median[state][day], values[3]);
    assert.ok(s.lower[state][day] <= s.median[state][day] && s.median[state][day] <= s.upper[state][day]);
    assert.ok(s.lower[state][day] >= values[0] && s.upper[state][day] <= values[6]);

    // Medians of a conserved population need not sum to n, but day 0 is fixed
    const day0 = s.states.reduce((sum, k) => sum + s.median[k][0], 0);
    assert.equal(day0, spec.n);
  } finally {
    ew.terminate();
  }
});

test("toCSV() writes one row per table row, with a header", async () => {
  const ew = await Epiworld.load({ workers: 0 });
  const res = await ew.run({ model: "SIR", n: 100, ndays: 3 });
  const lines = res.toCSV().trimEnd().split("\n");
  assert.equal(lines[0], "sim_id,date,nviruses,state,counts");
  assert.equal(lines.length, 1 + 4 * 3);
  assert.match(lines[1], /^0,0,1,Susceptible,\d+$/);
  assert.throws(() => res.toCSV("transition"), /No "transition" output/);
});
