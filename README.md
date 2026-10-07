# epiworldjs

[![npm](https://img.shields.io/npm/v/epiworldjs)](https://www.npmjs.com/package/epiworldjs)
[![CI](https://github.com/UofUEpiBio/epiworldjs/actions/workflows/ci.yml/badge.svg)](https://github.com/UofUEpiBio/epiworldjs/actions/workflows/ci.yml)
[![Playground](https://img.shields.io/badge/playground-GitHub%20Pages-0072b2)](https://uofuepibio.github.io/epiworldjs/)

**epiworldjs** runs the [epiworld](https://github.com/UofUEpiBio/epiworld)
agent-based epidemiological simulation engine in the browser (and in Node)
by compiling it to WebAssembly. It puts *real* epiworld models -- the same
C++ engine behind [epiworldR](https://github.com/UofUEpiBio/epiworldR) and
[epiworldpy](https://github.com/UofUEpiBio/epiworldpy) -- on any static
website (GitHub Pages, Quarto, R Markdown, a course page, a blog) with no
server: every simulation runs in the visitor's browser.

Before, putting epiworld on the web meant R Shiny
([epiworldRShiny](https://github.com/UofUEpiBio/epiworldRShiny)) and a
running R server. With epiworldjs it takes a script tag.

**Try it:** the [playground](https://uofuepibio.github.io/epiworldjs/) has
every model, with sliders.

## Install

From a CDN, with nothing to install:

```html
<script type="module"
  src="https://cdn.jsdelivr.net/npm/epiworldjs/dist/epiworld-model.js"></script>
```

or from npm, for bundlers and Node:

```sh
npm install epiworldjs
```

To pin a version, add it to the URL:
`https://cdn.jsdelivr.net/npm/epiworldjs@0.18.0-0/dist/epiworld-model.js`.
unpkg works too (`https://unpkg.com/epiworldjs/dist/epiworld-model.js`).

## The easy way: a web component

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
  controls="Contact rate, Prob. Transmission"
  autorun live>
</epiworld-model>
```

The `<epiworld-model>` element:

- builds sliders from the model's parameter metadata (only for the
  parameters listed in `controls`, or all of them with `controls="all"`);
- has a **Run** button, an `autorun` attribute, and a `live` attribute
  (debounced re-runs while sliders move);
- draws a built-in, dependency-free SVG chart of the state counts over
  time -- with `nsims > 1`, the median and a 95% band -- plus a summary
  (peak active cases, day of peak, outbreak size);
- offers `model-picker` (a drop-down with every model, i.e., a static
  Shiny-like playground) and **Download CSV**;
- is themeable with CSS custom properties (`--epiworld-color-<state>`,
  `--epiworld-accent`, `--epiworld-font`, ...), `::part()` selectors, and
  follows light/dark mode;
- takes `hide="Susceptible, ..."` for states hidden at first (the legend
  toggles them);
- emits an `epiworld-result` event and exposes `.result`, so pages can do
  their own plotting.

### In Quarto or R Markdown

The same two tags go in a raw HTML block:

````markdown
```{=html}
<script type="module"
  src="https://cdn.jsdelivr.net/npm/epiworldjs/dist/epiworld-model.js"></script>
<epiworld-model model="MeaslesSchool" nsims="50"
  params='{"Vaccination rate": 0.8, "Quarantine willingness": 0}'
  controls="Vaccination rate, Quarantine willingness" autorun live>
</epiworld-model>
```
````

## The flexible way: a JavaScript API

```js
// With npm: import { Epiworld } from "epiworldjs";
import { Epiworld } from "https://cdn.jsdelivr.net/npm/epiworldjs/dist/epiworld-model.js";

const ew = await Epiworld.load();     // starts a pool of Web Workers
ew.models();                          // metadata for every model: params, defaults, ranges

const res = await ew.run({
  model: "SEIRCONN",
  n: 10000,
  prevalence: 0.01,
  ndays: 100,
  nsims: 50,
  seed: 1,
  params: { "Contact rate": 4, "Prob. Transmission": 0.1 },
  outputs: ["total_hist", "transition", "reproductive"]
});

res.summary();   // median + 2.5% / 97.5% bands per state and day
res.toCSV();     // any output table, with epiworld's CSV columns
```

The same code runs in Node (20+), where the workers are `worker_threads`.

Parameter names are the epiworld names (`"Contact rate"`,
`"Prob. Transmission"`, ...), the same ones you use in epiworldR and
epiworldpy. They vary between models (SIRCONN has `"Transmission rate"`,
SEIRCONN `"Prob. Transmission"`); `ew.models()` lists each model's
parameters, and an unknown name is an error.

## Models

epiworld's built-in models and the
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
- **Results in memory, no files.** Simulations are collected with
  epiworld's `SaverMemory`
  ([UofUEpiBio/epiworld#290](https://github.com/UofUEpiBio/epiworld/pull/290)),
  as tables with the same columns as epiworld's CSV files -- the same
  mechanism epiworldR and epiworldpy can use.
- **Parallelism without special headers.** Instead of WASM threads (which
  need COOP/COEP headers that GitHub Pages cannot set), simulations are
  split across a pool of Web Workers. Per-simulation seeds are drawn up
  front exactly as `run_multiple` does, so results are identical for any
  number of workers -- and identical to native epiworld for the same seed.
- **CDN-friendly.** Workers start from a `Blob` URL and the `.wasm` file
  is located from `import.meta.url`, so loading from jsDelivr/unpkg on
  another origin works.
- **No runtime dependencies.** Plain ES modules documented with JSDoc; no
  framework, no bundler required.

### Repository layout

```
vendor/epiworld/   copy of epiworld's include/epiworld/   (make update-epiworld)
vendor/measles/    copy of measles' inst/include/measles/ (make update-measles)
vendor/VERSIONS    source commit SHAs and versions
cpp/               registry.hpp, core.hpp, bindings.cpp, golden.cpp
src/               index.js, worker.js, core.js, result.js, element.js, chart.js
site/              the GitHub Pages site and playground
test/              Node tests; e2e/ has the Playwright tests and a dev server
```

The WASM build uses [Emscripten](https://emscripten.org/) from a pinned
`emscripten/emsdk` container image (`.devcontainer/`), roughly:

```sh
emcc -O3 -std=c++17 -fwasm-exceptions -lembind \
  -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node \
  -sALLOW_MEMORY_GROWTH -sEXPORT_NAME=createEpiworldModule ...
```

## Testing

- **Golden equivalence:** the same specs run through a native build
  (clang with libc++, the standard library Emscripten uses) and the WASM
  build must produce byte-identical outputs for every model.
- **Worker invariance:** 0, 1, 2, and 4 workers give identical results.
- **Node tests:** every registered model runs with its defaults, conserves
  the population, and rejects unknown or out-of-range parameters with a
  clear error.
- **Playwright smoke test:** the playground renders, re-runs when a
  slider moves, exports CSV, and loads cross-origin.

## Versions and releases

Versions are epiworld's `X.Y.Z` followed by `-N`, as in epiworldR and
epiworldpy: `0.18.0-0` is the first epiworldjs release built on epiworld
0.18.0. See [RELEASING.md](RELEASING.md).

## Roadmap

The implementation followed [plan.md](plan.md); all of its v1 steps are done
(the engine and registry, the measles models, the worker pool, the element
and the playground, and npm releases). Next:

- an MCP server so AI assistants can run epiworld models
  ([#10](https://github.com/UofUEpiBio/epiworldjs/issues/10));
- step-by-step runs for animations, custom models defined in JS, network
  visualization, and calibration.

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
