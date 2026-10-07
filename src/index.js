/**
 * The epiworldjs API: runs epiworld models on a pool of workers (Web Workers
 * in the browser, worker_threads in Node).
 *
 * Each run splits its simulations into contiguous slices, one per worker.
 * Every simulation gets the seed `run_multiple()` would give it, so results
 * are identical for any number of workers.
 */

import { Core } from "./core.js";
import { Result } from "./result.js";

export { Core, Result };

const isNode = typeof process !== "undefined" && process.versions?.node != null &&
  typeof window === "undefined";

/** Browsers refuse `new Worker(url)` across origins (e.g. from a CDN), but a
 * same-origin blob that imports the worker module is fine. */
function browserWorker(url) {
  const blob = new Blob([`import ${JSON.stringify(url.href)};`], { type: "text/javascript" });
  const blobUrl = URL.createObjectURL(blob);
  const worker = new Worker(blobUrl, { type: "module" });
  URL.revokeObjectURL(blobUrl);
  return {
    post: (message) => worker.postMessage(message),
    listen: (fun) => {
      worker.onmessage = (event) => fun(event.data);
      worker.onerror = (event) => fun({ crashed: event.message ?? "The worker failed to start." });
    },
    busy: () => {},
    idle: () => {},
    terminate: () => worker.terminate(),
  };
}

async function nodeWorker(url) {
  const { Worker } = await import("node:worker_threads");
  // Workers inherit the parent's flags, but --input-type (as in
  // `node --input-type=module -e ...`) is only valid for the main script.
  // execArgv is only set then: an explicit one is validated, and some
  // inherited flags (e.g. the test runner's) would be rejected.
  const execArgv = process.execArgv.filter((arg) => !arg.startsWith("--input-type"));
  const worker = new Worker(url, execArgv.length < process.execArgv.length ? { execArgv } : {});
  return {
    post: (message) => worker.postMessage(message),
    listen: (fun) => {
      worker.on("message", fun);
      worker.on("error", (error) => fun({ crashed: error.message }));
      worker.unref(); // After the listeners, which ref it again
    },
    // An idle pool does not keep Node running; a pending run does
    busy: () => worker.ref(),
    idle: () => worker.unref(),
    terminate: () => worker.terminate(),
  };
}

function defaultWorkers() {
  const n = globalThis.navigator?.hardwareConcurrency ?? 2;
  return Math.max(1, Math.min(n, 4));
}

/** Splits 0..nsims-1 into at most `k` contiguous, non-empty slices. */
function slices(nsims, k) {
  const out = [];
  const base = Math.floor(nsims / k), extra = nsims % k;
  for (let i = 0, start = 0; i < k && start < nsims; i++) {
    const size = base + (i < extra ? 1 : 0);
    out.push(Array.from({ length: size }, (_, j) => start + j));
    start += size;
  }
  return out;
}

/** Concatenates the tables of consecutive slices, column by column. */
function concat(parts) {
  const out = {};
  for (const [name, table] of Object.entries(parts[0])) {
    out[name] = {};
    for (const column of Object.keys(table)) {
      const pieces = parts.map((p) => p[name][column]);
      if (Array.isArray(pieces[0])) {
        out[name][column] = pieces.flat();
      } else {
        const all = new pieces[0].constructor(pieces.reduce((n, p) => n + p.length, 0));
        let offset = 0;
        for (const p of pieces) { all.set(p, offset); offset += p.length; }
        out[name][column] = all;
      }
    }
  }
  return out;
}

export class Epiworld {
  /**
   * Loads the WebAssembly module and starts the workers.
   *
   * @param {{workers?: number}} [options] Number of workers (default:
   *   the number of cores, at most 4). 0 runs everything on the calling
   *   thread.
   * @returns {Promise<Epiworld>}
   */
  static async load({ workers = defaultWorkers() } = {}) {
    if (!(Number.isInteger(workers) && workers >= 0))
      throw new Error("workers must be a non-negative integer.");

    const core = await Core.load();
    const url = new URL("./worker.js", import.meta.url);
    const pool = await Promise.all(
      Array.from({ length: workers }, () => (isNode ? nodeWorker(url) : browserWorker(url))),
    );
    return new Epiworld(core, pool);
  }

  constructor(core, pool) {
    this.core = core;
    this.pool = pool;
    this._pending = new Map();
    this._next = 0;
    for (const worker of pool) {
      worker.inFlight = 0;
      worker.listen((message) => {
        if (message.crashed) {
          for (const { reject } of this._pending.values()) reject(new Error(message.crashed));
          this._pending.clear();
          return;
        }
        const pending = this._pending.get(message.id);
        if (!pending) return;
        this._pending.delete(message.id);
        if (--worker.inFlight === 0) worker.idle();
        if (message.error) pending.reject(new Error(message.error));
        else pending.resolve(message.tables);
      });
    }
  }

  /** Number of workers in the pool. */
  get workers() {
    return this.pool.length;
  }

  /** Metadata for every model: parameters, defaults, limits and states. */
  models() {
    return this.core.models();
  }

  /** @returns {string[]} */
  outputNames() {
    return this.core.outputNames();
  }

  /** @returns {{epiworld: string, measles: string}} */
  version() {
    return this.core.version();
  }

  _send(worker, spec, simIds) {
    return new Promise((resolve, reject) => {
      const id = this._next++;
      this._pending.set(id, { resolve, reject });
      if (worker.inFlight++ === 0) worker.busy();
      worker.post({ id, spec, simIds });
    });
  }

  /**
   * Runs `spec`, splitting its simulations across the workers.
   *
   * @param {import("./core.js").RunSpec} spec
   * @returns {Promise<Result>} Rejects with an `Error` naming the problem
   *   if the spec is invalid.
   */
  async run(spec) {
    const model = this.models().find((m) => m.id === spec?.model);
    const nsims = spec?.nsims ?? 1;

    let tables;
    if (this.pool.length === 0 || !model || !(Number.isInteger(nsims) && nsims >= 1)) {
      // On this thread; an invalid spec throws here with the core's message
      tables = this.core.run(spec);
    } else {
      const parts = slices(nsims, this.pool.length);
      tables = concat(await Promise.all(parts.map((ids, i) => this._send(this.pool[i], spec, ids))));
    }
    return new Result(spec, tables, model);
  }

  /** Stops the workers; later runs happen on the calling thread. */
  terminate() {
    for (const worker of this.pool) worker.terminate();
    this.pool = [];
    for (const { reject } of this._pending.values()) reject(new Error("The pool was terminated."));
    this._pending.clear();
  }
}
