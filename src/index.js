/**
 * The epiworldjs API.
 *
 * For now runs happen on the calling thread; the next roadmap stage moves
 * them to a pool of Web Workers behind the same `run(spec)`.
 */

import { Core } from "./core.js";

export { Core };

export class Epiworld {
  /** @returns {Promise<Epiworld>} */
  static async load() {
    return new Epiworld(await Core.load());
  }

  /** @param {Core} core */
  constructor(core) {
    this.core = core;
  }

  /** Metadata for every model: parameters, defaults, limits and states. */
  models() {
    return this.core.models();
  }

  /**
   * @param {import("./core.js").RunSpec} spec
   * @returns {Promise<import("./core.js").Tables>}
   */
  async run(spec) {
    return this.core.run(spec);
  }
}
