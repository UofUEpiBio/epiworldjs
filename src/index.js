/**
 * Load the epiworld WebAssembly module.
 *
 * This initial scaffold exposes SIRCONN directly. The public `run(spec)` API
 * and model registry are intentionally deferred to the next roadmap stage.
 */
export class Epiworld {
  static async load() {
    const { default: createEpiworldModule } = await import("../dist/core.js");
    const module = await createEpiworldModule();
    return new Epiworld(module);
  }

  constructor(module) {
    this.module = module;
  }

  /** Run epiworld's connected-population SIR model. */
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

