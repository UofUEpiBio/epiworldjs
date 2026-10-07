// A static file server for the site and the e2e tests (no dependencies).
// Usage: node test/e2e/serve.mjs [port]
// Lays files out as GitHub Pages does: site/ at the root, with dist/ and src/
// (and test/, for the e2e pages) beside it. Responses allow any origin, like
// a CDN, so a page on another port can load the package from this one.

import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("../..", import.meta.url));
const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json",
  ".csv": "text/csv",
};

export function serve(port = 0) {
  const server = createServer(async (req, res) => {
    const path = normalize(decodeURIComponent(new URL(req.url, "http://x").pathname));
    const base = /^\/(dist|src|test)\//.test(path) ? root : join(root, "site");
    let file = join(base, path);
    if (!file.startsWith(base)) return res.writeHead(403).end();
    if (file.endsWith("/")) file = join(file, "index.html");
    try {
      const body = await readFile(file);
      res.writeHead(200, {
        "Content-Type": types[extname(file)] ?? "application/octet-stream",
        "Access-Control-Allow-Origin": "*",
        "Cache-Control": "no-store",
      });
      res.end(body);
    } catch {
      res.writeHead(404).end("Not found");
    }
  });
  return new Promise((resolve) => server.listen(port, "127.0.0.1", () => resolve(server)));
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const server = await serve(Number(process.argv[2] ?? 8000));
  console.log(`Serving the site at http://127.0.0.1:${server.address().port}/`);
}
