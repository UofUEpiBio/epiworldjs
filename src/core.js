/**
 * Synchronous access to the WebAssembly core, on the calling thread.
 *
 * `Epiworld` (index.js) is the main API; this is what it builds on, and what
 * Node scripts and tests can use directly.
 */

let modulePromise;

// Instantiates the module once; a failed load is retried on the next call.
function loadModule() {
  // core.wasm is found next to core.js, wherever this module was loaded from
  // (a CDN, or a worker started from a blob: URL)
  const url = new URL("../dist/core.js", import.meta.url);
  modulePromise ??= import(url.href)
    .then(({ default: createEpiworldModule }) =>
      createEpiworldModule({ locateFile: (file) => new URL(file, url).href }))
    .catch((error) => {
      modulePromise = undefined;
      throw error;
    });
  return modulePromise;
}

/**
 * @typedef {object} ParamInfo
 * @property {string} name epiworld's name for the parameter.
 * @property {number} value Default value.
 * @property {number} min
 * @property {number} max
 * @property {number} step Suggested slider step.
 * @property {boolean} integer
 * @property {string} description
 *
 * @typedef {object} ModelInfo
 * @property {string} id For example `"SEIRCONN"`.
 * @property {string} label
 * @property {string} family `"basic"`, `"connected"`, `"mixing"` or `"measles"`.
 * @property {string} population `"network"`, `"connected"` or `"mixing"`.
 * @property {string[]} states State labels, in epiworld's order.
 * @property {ParamInfo[]} params
 *
 * @typedef {object} RunSpec
 * @property {string} model A model id from `models()`.
 * @property {Object<string, number>} [params] By epiworld name; unset ones
 *   take their defaults.
 * @property {number} [n] Number of agents (the model's default: 10000 for
 *   most models, 500 for MeaslesSchool, 9000 for the measles mixing models).
 * @property {number} [prevalence] Initial proportion infected (the model's
 *   default: 0.01 for most, one case for the measles models). MeaslesSchool
 *   starts with round(prevalence * n) cases.
 * @property {number} [ndays] Default 100.
 * @property {number} [nsims] Default 1.
 * @property {number} [seed] Default 1.
 * @property {object} [population] `{type: "smallworld", k, p, directed}`,
 *   `{type: "edgelist", source, target, directed}` (network models), or
 *   `{type: "groups", sizes, contact_matrix}` (mixing models; the matrix as
 *   rows, entry [i][j] = daily contacts of group i with group j).
 * @property {string[]} [outputs] Names from `outputNames()` (default
 *   `["total_hist"]`).
 *
 * @typedef {Object<string, Object<string, Int32Array | Float64Array | string[]>>} Tables
 *   Output name → column name → values. Columns match epiworld's CSV files,
 *   preceded by a 0-based `sim_id`.
 */

export class Core {
  /** @returns {Promise<Core>} */
  static async load() {
    return new Core(await loadModule());
  }

  constructor(module) {
    this.module = module;
    this._models = undefined;
  }

  /** @returns {ModelInfo[]} */
  models() {
    this._models ??= this.module.listModels();
    return this._models;
  }

  /** @returns {string[]} */
  outputNames() {
    return this.module.outputNames();
  }

  /** @returns {{epiworld: string, measles: string}} */
  version() {
    return this.module.version();
  }

  /**
   * Runs the simulations of `spec`, or only those in `simIds`. Invalid specs
   * throw an `Error` that names the problem.
   *
   * @param {RunSpec} spec
   * @param {number[]} [simIds] Which of `0..nsims-1` to run; all by default.
   * @returns {Tables}
   */
  run(spec, simIds) {
    return this.module.run(spec, simIds ?? null);
  }
}
