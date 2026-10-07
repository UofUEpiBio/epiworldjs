/**
 * Runs simulations for the pool in index.js. Works as a browser module
 * worker and as a Node worker_threads worker.
 *
 * Messages in:  {id, spec, simIds}
 * Messages out: {id, tables} or {id, error}
 */

import { Core } from "./core.js";

const port = typeof self === "undefined"
  ? (await import("node:worker_threads")).parentPort
  : self;

const corePromise = Core.load();

// Typed-array columns are transferred, not copied
function buffers(tables) {
  const out = [];
  for (const table of Object.values(tables))
    for (const column of Object.values(table))
      if (ArrayBuffer.isView(column)) out.push(column.buffer);
  return out;
}

async function handle({ id, spec, simIds }) {
  try {
    const tables = (await corePromise).run(spec, simIds);
    port.postMessage({ id, tables }, buffers(tables));
  } catch (error) {
    port.postMessage({ id, error: error?.message ?? String(error) });
  }
}

if (typeof self === "undefined") port.on("message", handle);
else self.onmessage = (event) => handle(event.data);
