/**
 * The outcome of `Epiworld.run()`: epiworld's output tables, plus summaries.
 */

/**
 * Quantile of sorted values, interpolating linearly between order statistics
 * (R's default, type 7).
 *
 * @param {Float64Array} sorted
 * @param {number} p
 */
function quantile(sorted, p) {
  const h = (sorted.length - 1) * p;
  const lo = Math.floor(h);
  const hi = Math.min(lo + 1, sorted.length - 1);
  return sorted[lo] + (h - lo) * (sorted[hi] - sorted[lo]);
}

function quote(value) {
  if (typeof value !== "string") return String(value);
  return /[",\n]/.test(value) ? `"${value.replaceAll('"', '""')}"` : value;
}

export class Result {
  /**
   * @param {import("./core.js").RunSpec} spec
   * @param {import("./core.js").Tables} tables
   * @param {import("./core.js").ModelInfo} model
   */
  constructor(spec, tables, model) {
    /** The spec that produced this result. */
    this.spec = spec;
    /** Output name → column name → values; see `Core.run()`. */
    this.tables = tables;
    /** The model's metadata, as in `Epiworld.models()`. */
    this.model = model;
  }

  /** State labels, in epiworld's order. */
  get states() {
    return this.model.states;
  }

  get nsims() {
    return this.spec.nsims ?? 1;
  }

  /**
   * Counts per state and day across simulations: the median and a central
   * interval (95% by default).
   *
   * @param {{level?: number}} [options]
   * @returns {{dates: Int32Array, states: string[], median: Object<string, Float64Array>,
   *   lower: Object<string, Float64Array>, upper: Object<string, Float64Array>}}
   */
  summary({ level = 0.95 } = {}) {
    const { sim_id, date, state, counts } = this.tables.total_hist ?? {};
    if (!counts) throw new Error('summary() needs the "total_hist" output.');

    const states = this.states;
    const ndays = date.reduce((max, d) => Math.max(max, d), 0) + 1;
    const nsims = this.nsims;
    const stateIndex = new Map(states.map((s, i) => [s, i]));

    // values[state][day] holds one count per simulation
    const values = states.map(() => Array.from({ length: ndays }, () => new Float64Array(nsims)));
    for (let i = 0; i < counts.length; i++)
      values[stateIndex.get(state[i])][date[i]][sim_id[i]] = counts[i];

    const alpha = (1 - level) / 2;
    const out = { dates: Int32Array.from({ length: ndays }, (_, d) => d), states, median: {}, lower: {}, upper: {} };
    states.forEach((s, k) => {
      for (const key of ["median", "lower", "upper"]) out[key][s] = new Float64Array(ndays);
      for (let d = 0; d < ndays; d++) {
        const sorted = values[k][d].sort();
        out.median[s][d] = quantile(sorted, 0.5);
        out.lower[s][d] = quantile(sorted, alpha);
        out.upper[s][d] = quantile(sorted, 1 - alpha);
      }
    });
    return out;
  }

  /**
   * One output table as CSV, with a header row.
   *
   * @param {string} [output] Default `"total_hist"`.
   */
  toCSV(output = "total_hist") {
    const table = this.tables[output];
    if (!table) throw new Error(`No "${output}" output in this result.`);
    const names = Object.keys(table);
    const columns = Object.values(table);
    const nrow = columns.length ? columns[0].length : 0;
    const lines = [names.map(quote).join(",")];
    for (let i = 0; i < nrow; i++) lines.push(columns.map((c) => quote(c[i])).join(","));
    return lines.join("\n") + "\n";
  }
}
