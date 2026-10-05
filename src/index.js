/**
 * Load the epiworld WebAssembly module.
 *
 * This initial scaffold exposes SIRCONN directly. The public `run(spec)` API
 * and model registry are intentionally deferred to the next roadmap stage.
 */

let modulePromise;

// Instantiates the module once; a failed load is retried on the next call.
function loadModule() {
  modulePromise ??= import("../dist/core.js")
    .then(({ default: createEpiworldModule }) => createEpiworldModule())
    .catch((error) => {
      modulePromise = undefined;
      throw error;
    });
  return modulePromise;
}

export class Epiworld {
  static async load() {
    return new Epiworld(await loadModule());
  }

  constructor(module) {
    this.module = module;
  }

  /**
   * Run epiworld's connected-population SIR model.
   *
   * @returns {{day: Int32Array, state: string[], count: Int32Array}} One row
   *   per day and state. Invalid arguments throw an `Error`.
   */
  runSIRCONN({
    n,
    prevalence,
    ndays,
    seed = 1,
    contactRate = 4,
    transmissionRate = 0.1,
    recoveryRate = 1 / 7,
  }) {
    return this.module.runSIRCONN(
      n,
      prevalence,
      ndays,
      seed,
      contactRate,
      transmissionRate,
      recoveryRate,
    );
  }
}
