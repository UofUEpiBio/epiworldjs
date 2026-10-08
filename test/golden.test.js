// The JS API reproduces the native C++ build exactly: every run in
// build/golden-native.txt (written by cpp/golden.cpp, `make
// build/golden-native.txt`) is replayed from its spec through core.run().
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import { Core } from "../src/core.js";

const core = await Core.load();
const golden = readFileSync(new URL("../build/golden-native.txt", import.meta.url), "utf8");

// "# <spec>", then per output "## <name>", its column names and rows
for (const block of golden.split(/^# /m).slice(1)) {
  const [header, ...outputs] = block.split(/^## /m);
  const spec = JSON.parse(header);
  const modified = Object.keys(spec.params).length ? "modified parameters" : "defaults";

  test(`${spec.model} with ${modified}, seed ${spec.seed}, matches native`, () => {
    const out = core.run(spec);
    assert.deepEqual(Object.keys(out).sort(), spec.outputs.slice().sort());

    for (const output of outputs) {
      const [name, colnames, ...rows] = output.trimEnd().split("\n");
      const table = out[name];
      const columns = colnames.split("\t");
      assert.deepEqual(Object.keys(table), columns, name);

      const cells = rows.map((row) => row.split("\t"));
      columns.forEach((column, j) => {
        const actual = Array.from(table[column]);
        const expected = cells.map((cell) => (typeof actual[0] === "string" ? cell[j] : Number(cell[j])));
        assert.deepEqual(actual, expected, `${name}.${column}`);
      });
    }
  });
}
