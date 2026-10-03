# epiworldjs

> **Status: planning / pre-alpha.** Nothing here is usable yet. This README
> describes the design we are building toward; the full implementation plan
> is in [`plan.md`](plan.md). Feedback is welcome in the issues.

**epiworldjs** runs the [epiworld](https://github.com/UofUEpiBio/epiworld)
agent-based epidemiological simulation engine in the browser by compiling
it to WebAssembly. The goal: put *real* epiworld models -- the same C++
engine behind [epiworldR](https://github.com/UofUEpiBio/epiworldR) and
[epiworldpy](https://github.com/UofUEpiBio/epiworldpy) -- on any static
website (GitHub Pages, Quarto, R Markdown, a course page, a blog) with no
server.

Today, putting epiworld on the web means R Shiny
([epiworldRShiny](https://github.com/UofUEpiBio/epiworldRShiny)) and a
running R server. epiworldjs replaces that with a script tag.

## What it will look like

### The easy way: a web component

One script tag, one HTML tag:

```html
<script type="module"
  src="https://cdn.jsdelivr.net/npm/epiworldjs/dist/epiworld-model.js"></script>

<epiworld-model
  model="SEIRCONN"
  ndays="100"
  nsims="30"
  n="10000"
  params='{"Contact rate": 4}'
  controls="Contact rate, Transmission rate">
</epiworld-model>
```

The `<epiworld-model>` element:

- builds sliders from the model's parameter metadata (only for the
  parameters listed in `controls`, or all of them with `controls="all"`);
- has a **Run** button, an `autorun` attribute, and a `live` attribute
  (debounced re-runs while sliders move);
- draws a built-in, dependency-free SVG chart of the state counts over
  time -- with `nsims > 1`, the median and a 95% band -- plus a summary
  (peak, day of peak, final size);
- offers `model-picker` (a drop-down with every model, i.e., a static
  Shiny-like playground) and **Download CSV**;
- is themeable with CSS custom properties (`--epiworld-color-<state>`,
  fonts), `::part()` selectors, and follows light/dark mode;
- emits an `epiworld-result` event and exposes `.result`, so pages can do
  their own plotting.

### The flexible way: a JavaScript API

```js
import { Epiworld } from "epiworldjs";

const ew = await Epiworld.load();     // starts a pool of Web Workers
ew.models();                          // metadata for every model: params, defaults, ranges

const res = await ew.run({
  model: "SEIRCONN",
  n: 10000,
  prevalence: 0.01,
  ndays: 100,
  nsims: 50,
  seed: 1,
  params: { "Contact rate": 4, "Transmission rate": 0.1 },
  outputs: ["total_hist", "transition", "reproductive"]
});

res.summary();   // median + 2.5% / 97.5% bands per state and day
res.toCSV();
```

Parameter names are the epiworld names (`"Transmission rate"`,
`"Contact rate"`, ...), the same ones you use in epiworldR and
epiworldpy.

## Models

v1 will cover epiworld's built-in models and the
[measles](https://github.com/UofUEpiBio/measles) models:

| Family | Models |
|---|---|
| Basic (network) | SIR, SIS, SEIR, SIRD, SISD, SEIRD |
| Connected (fully mixed) | SIRCONN, SEIRCONN, SIRDCONN, SEIRDCONN |
| Mixing (groups + contact matrix) | SIRMixing, SEIRMixing, SEIRMixingQuarantine |
| Network + quarantine | SEIRNetworkQuarantine |
| Measles | MeaslesSchool, MeaslesMixing, MeaslesMixingRiskQuarantine |

Populations can be a small-world network, a user-supplied edge list, or
groups with a contact matrix, depending on the model.

## Design

```
<epiworld-model> web component  (src/element.js, src/chart.js)   <- easy layer
        | uses
Epiworld.run(spec) -> Promise<Result>  (src/index.js)            <- JS API
        | postMessage(spec) to a pool of Web Workers (src/worker.js)
WASM core: embind glue (cpp/bindings.cpp) -> spec -> model -> results (cpp/core.hpp)
        |
vendor/epiworld + vendor/measles  (header-only C++, vendored)
```

- **One model registry** (`cpp/registry.hpp`) is the single source of
  truth: model id, family, parameters (name, default, min, max, step,
  description), and a builder that calls the real C++ constructor. The JS
  API and the web component's forms are generated from it.
- **Declarative specs, not bound classes.** JS sends a plain object;
  C++ builds the model, runs it, and returns typed arrays. Specs are easy
  to send to workers, store in URLs, and share.
- **Results in memory, no files.** Simulations are collected with an
  in-memory saver (proposed upstream in epiworld; see
  [UofUEpiBio/epiworld#288](https://github.com/UofUEpiBio/epiworld/issues/288)),
  the same mechanism epiworldR and epiworldpy can use.
- **Parallelism without special headers.** Instead of WASM threads (which
  need COOP/COEP headers that GitHub Pages cannot set), simulations are
  split across a pool of Web Workers. Per-simulation seeds are drawn up
  front exactly as `run_multiple` does, so results are identical for any
  number of workers -- and identical to native epiworld for the same seed.
- **CDN-friendly.** Workers start from a `Blob` URL and the `.wasm` file
  is located from `import.meta.url`, so loading from jsDelivr/unpkg on
  another origin works.
- **No runtime dependencies.** Plain ES modules (JSDoc + generated
  `.d.ts`), no framework, no bundler required.

### Repository layout (planned)

```
vendor/epiworld/   copy of epiworld's include/epiworld/   (make update-epiworld)
vendor/measles/    copy of measles' inst/include/measles/ (make update-measles)
vendor/VERSIONS    source commit SHAs and versions
cpp/               registry.hpp, core.hpp, bindings.cpp, golden.cpp
src/               index.js, worker.js, core.js, element.js, chart.js, summarize.js
site/              GitHub Pages gallery + playground, Quarto example
test/              Node tests, golden outputs, Playwright smoke test
```

The WASM build uses [Emscripten](https://emscripten.org/) from a pinned
`emscripten/emsdk` container image (`.devcontainer/`), roughly:

```sh
emcc -O3 -std=c++17 -fwasm-exceptions -lembind \
  -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node \
  -sALLOW_MEMORY_GROWTH -sEXPORT_NAME=createEpiworldModule ...
```

## Testing (planned)

- **Golden equivalence:** the same specs run through a native `g++` build
  and the WASM build must produce identical outputs for every model.
- **Worker invariance:** 1, 2, and 4 workers give identical results.
- **Node tests:** every registered model runs with its defaults, conserves
  the population, and rejects unknown or out-of-range parameters with a
  clear error.
- **Playwright smoke test:** the playground renders, re-runs when a
  slider moves, exports CSV, and loads cross-origin.

## Roadmap

Follow the implementation order in [plan.md](plan.md). Its upstream prerequisite
is proposed in [epiworld PR #289](https://github.com/UofUEpiBio/epiworld/pull/289);
that change must merge before updating the vendored headers and proceeding.

1. epiworld: in-memory savers for `run_multiple` and wasm32 fixes.
2. Scaffold: vendoring, devcontainer, minimal SIRCONN running in Node.
   **Incomplete:** the native smoke test passes, but the WASM build and Node
   loader remain unverified. The upstream prerequisites in `plan.md` and the
   correctness findings on PR #1 must be addressed before this stage is done.
3. Model registry + core for the built-in models; golden tests.
4. Measles models.
5. Worker pool and `Epiworld` JS API.
6. `<epiworld-model>` and the SVG chart; GitHub Pages playground.
7. CI, npm publishing, docs with copy-paste HTML and Quarto snippets.

Later: step-by-step runs for animations, custom models defined in JS,
network visualization, calibration.

## Related projects

- [epiworld](https://github.com/UofUEpiBio/epiworld) -- the C++ engine.
- [epiworldR](https://github.com/UofUEpiBio/epiworldR) and
  [epiworldpy](https://github.com/UofUEpiBio/epiworldpy) -- R and Python
  bindings.
- [epiworldRShiny](https://github.com/UofUEpiBio/epiworldRShiny) -- the
  Shiny app.
- [measles](https://github.com/UofUEpiBio/measles) -- measles models built
  on epiworld.

## License

MIT, like epiworld.
