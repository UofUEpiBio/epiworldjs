# epiworldjs implementation plan

> Living design document. Line references to epiworld sources (`include/epiworld/...`, `tests/...`) point to [UofUEpiBio/epiworld@339ca7c](https://github.com/UofUEpiBio/epiworld/tree/339ca7c) (v0.18.0). The upstream saver proposal is tracked in [UofUEpiBio/epiworld#288](https://github.com/UofUEpiBio/epiworld/issues/288).

## Context

Today, the only way to put epiworld on the web is R Shiny (epiworldRShiny), and that needs an R server. The goal is a new repo, **`UofUEpiBio/epiworldjs`**, that compiles the C++ engine to WebAssembly so a static site (GitHub Pages, Quarto, a blog, a course page) can run real epiworld simulations in the visitor's browser, with no server.

`UofUEpiBio/epiworldweb` doesn't fit. Despite its description ("WASM port"), it's a SvelteKit block editor that generates R/Python code, and it contains no engine. We start fresh and keep it small.

Decisions so far:
- **Scope:** the engine, plus something "people can use very easily". That means a drop-in `<epiworld-model>` web component.
- **Models:** all built-in epiworld models plus the measles models (the `UofUEpiBio/measles` headers).
- **Name:** `epiworldjs`. The repo is `UofUEpiBio/epiworldjs` and the npm package is `epiworldjs`; the npm name is currently free.

What a survey of the epiworld code found:
- Nothing blocks a WASM build.
  - OpenMP is fully `#ifdef _OPENMP`-guarded (`include/epiworld/config.hpp:19`), and `run_multiple` has a serial `#else` branch (`model-meat.hpp:1831`).
  - There's no `std::thread`, `system()`, `getenv` or `std::filesystem`. The RNG is a custom xoshiro256** that uses explicit `uint64_t` (`rng-utils.hpp:19`).
  - All printing goes through the overridable `printf_epiworld` macro (`config.hpp:4`).
- One wasm32 bug: `postsampling-meat.hpp:31` does `size_t(1) << 32`. That's undefined behaviour when `size_t` is 32 bits (Clang warns about it).
- Trap: `run_multiple`'s default callback `make_save_run()` writes CSV files. The wrapper must never use it; it passes its own callback or runs sims itself.
- `epiworld_fast_uint` is `unsigned long long`, which embind maps to BigInt. The wrapper takes JS numbers and casts them, so the JS API never sees BigInt.
- Per-sim seeds in `run_multiple` are drawn from the master seed up front (`model-meat.hpp:1842-1851`). So a pool of Web Workers can reproduce `run_multiple` exactly, whatever the worker count.

## Architecture (three layers)

```
<epiworld-model> web component  (src/element.js, src/chart.js)   ← "easy" layer
        │ uses
Main-thread API: Epiworld.run(spec) → Promise<Result>  (src/index.js)
        │ postMessage(spec) to a pool of Web Workers (src/worker.js)
WASM core: embind glue (cpp/bindings.cpp) → spec→model→results (cpp/core.hpp)
        │
vendor/epiworld + vendor/measles   (header-only C++, vendored)
```

### 1. C++ core: a declarative, registry-driven API (`cpp/`)
Instead of binding each model class and its long positional constructor, the core exposes **one model registry**. It is the single source of truth for both the engine and the UI.

- `cpp/registry.hpp` has one entry per model, with:
  - `id` (`"SIR"`, `"SEIRCONN"`, `"SIRMixing"`, `"MeaslesSchool"`, …), `label`, `family` (basic / connected / mixing / measles), and a `population` kind (network / connected / mixing / built-in).
  - `params`: a list of `{name, default, min, max, step, integer, description}`. `name` is the **epiworld parameter name** (`"Transmission rate"`, `"Contact rate"`), the same names as `get_param` and epiworldR. Defaults come from epiworldR / epiworldRShiny.
  - A `build(const RunSpec&)` lambda that calls the real constructor (`ModelSEIRCONN<>(...)`, `ModelMeaslesSchool<>(...)`, …).
- Models covered: SIR, SIS, SEIR, SIRD, SISD, SEIRD, the CONN variants (SIR, SEIR, SIRD, SEIRD), the Mixing variants (SIR, SEIR), SEIRMixingQuarantine and SEIRNetworkQuarantine from `include/epiworld/models/`, plus MeaslesSchool, MeaslesMixing and MeaslesMixingRiskQuarantine. SIRLogit, DiffNet and SURV are left out of v1; they need raw data or covariates.
- `cpp/core.hpp` is plain C++ with no Emscripten dependency, so it builds natively for golden tests.
  - `RunSpec` holds: `model`, `params` (map), `prevalence`, `population`, `ndays`, `seed`, `sim_seeds` (vector), and `outputs` (flags).
  - `population` is one of:
    - `{type: "smallworld", n, k, p}` via `agents_smallworld`
    - `{type: "edgelist", source, target, n, directed}` via `agents_from_edgelist`
    - `{type: "groups", sizes[], contact_matrix[]}`, with entities set up as in `tests/05-mixing.cpp:31-37`
  - `RunResult` holds:
    - `hist_total`, from `DataBase::get_hist_total`
    - optionally `transitions` (`get_hist_transition_matrix`, `skip_zeros`), `rt` (`get_reproductive_number`), `generation_time`, and `transmissions` (`get_transmissions`)
    - for the measles models, `hospitalizations` (`get_hospitalizations`)
  - `run_many(spec, sim_indices)` resets from a backup and runs `run(ndays, sim_seeds[i])` for each index, passing each run to a `SaverMemory` (from upstream change A). It never touches files, and the result tables are the same ones epiworldR/epiworldpy will use. When everything runs in one worker, `run_multiple(..., saver, ...)` is used directly.
  - `draw_sim_seeds(spec, nsims)` reproduces the seed draw in `run_multiple`.
- `cpp/bindings.cpp` is the embind glue and only converts between `emscripten::val` and the structs.
  - It reads JS objects with `val::operator[]`.
  - It returns results as `Int32Array`/`Float64Array` copies made from `typed_memory_view`.
  - It wraps everything in `try/catch` and turns C++ exceptions into JS `Error`s carrying epiworld's message.
  - It exports `listModels()`, which returns the registry metadata, and `version()`, which returns the epiworld and measles versions.
- Define `printf_epiworld` before including epiworld so verbose output goes to a buffer or is turned off. `verbose_off()` is the default.

### 2. JS API (`src/`, plain ES modules with JSDoc; `tsc --emitDeclarationOnly` produces `.d.ts`)
```js
import { Epiworld } from "epiworldjs";
const ew = await Epiworld.load();            // spins up a worker pool
ew.models();                                 // registry metadata → build your own UI
const res = await ew.run({ model: "SEIRCONN", ndays: 100, seed: 1, nsims: 50,
  params: { "Contact rate": 4, "Prob. Transmission": 0.1 }, n: 10000, prevalence: 0.01 });
res.days; res.states; res.counts;            // per-sim typed arrays
res.summary();                               // median + 2.5/97.5% bands per state
res.toCSV();
```
- `src/worker.js` loads the Emscripten ES6 module and answers `run`/`models` messages.
- `src/index.js` holds a pool of `min(navigator.hardwareConcurrency, 4)` workers. It draws `sim_seeds` once, splits the sim indices across workers and merges the results. Output is identical for any worker count and identical to `run_multiple`.
- Cross-origin CDN use: browsers refuse `new Worker(crossOriginURL)`, so the worker is started from a `Blob` URL that does `import "<absolute URL of worker.js>"`. The `.wasm` file is located from `import.meta.url`.
- `src/core.js` is a no-worker path, used in Node tests and by anyone who wants synchronous calls.
- No pthreads or SharedArrayBuffer. Those need COOP/COEP headers, which GitHub Pages can't set. The parallelism comes from the worker pool instead.

### 3. The "easy" layer: `<epiworld-model>` web component (`src/element.js`)
One script tag and one HTML tag, and it works in Quarto, R Markdown, plain HTML or a CMS:
```html
<script type="module" src="https://cdn.jsdelivr.net/npm/epiworldjs/dist/epiworld-model.js"></script>
<epiworld-model model="SEIRCONN" ndays="100" nsims="30" n="10000"
  params='{"Contact rate": 4}' controls="Contact rate, Prob. Transmission"></epiworld-model>
```
- It generates sliders from the registry metadata, but only for the parameters listed in `controls`; `controls="all"` shows every parameter.
- It has a Run button, with auto-run on load (`autorun`) and debounced re-runs as sliders move (`live`).
- **Built-in SVG chart** (`src/chart.js`, no dependency) shows state counts over time. With `nsims > 1` it draws the median line plus a 95% band. It also shows a summary row: peak, day of peak and final size.
- Attributes `model-picker` (a model drop-down, which gives a Shiny-like playground) and `outputs` (`curve`, `table`, `rt`).
- It's themeable with CSS custom properties (`--epiworld-color-<state>`, fonts), `::part()` hooks, and light/dark via `prefers-color-scheme`.
- It fires an `epiworld-result` event and exposes `.result`, so pages can do their own plotting. A "Download CSV" button is included.

## Repo layout

```
epiworldjs/
  vendor/epiworld/   rsync of epiworld include/epiworld/   (same pattern as epiworldpy's Makefile)
  vendor/measles/    rsync of measles inst/include/measles/
  vendor/VERSIONS    source commit SHAs + versions
  cpp/  registry.hpp core.hpp bindings.cpp golden.cpp
  src/  index.js worker.js core.js element.js chart.js summarize.js
  dist/ (build output; gitignored, published to npm)
  site/ index.html (gallery + playground), quarto-example.qmd
  test/ node/*.test.js  golden/*.json  e2e/smoke.spec.js
  Makefile  package.json  .devcontainer/Containerfile (FROM emscripten/emsdk:<pinned>, + node, g++)
  .github/workflows/ ci.yml pages.yml publish.yml please-bump.yml
  AGENTS.md  README.md  LICENSE (MIT)  CITATION.cff
```
- `Makefile` targets:
  - `update-epiworld` / `update-measles`, with `local-` and GitHub variants copied from epiworldpy's Makefile.
  - `wasm`: `emcc -O3 -std=c++17 -fwasm-exceptions -lembind -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH -sEXPORT_NAME=createEpiworldModule`. No `-fopenmp` and no `-march=native`.
  - `golden` (native g++), `test`, `serve`.
- The build runs in a container (podman or docker locally), and CI uses the same image.

## Upstream change A (epiworld, done first): savers for `run_multiple` ([#288](https://github.com/UofUEpiBio/epiworld/issues/288))

**Problem.** The way `run_multiple` stores results is tied to files.
- Its default callback, `make_save_run()` (`include/epiworld/model-meat.hpp:41`), takes 12 positional bools plus a `printf` pattern, and writes CSVs through `DataBase::write_data`.
- epiworldR (`src/model.cpp:53`, `R/make_saver.R:149-233`) writes those CSVs to a temp directory and then parses them back with `read.table`. epiworldpy does the same.
- In WASM, the files land in MEMFS and would have to be parsed again in JS.
- Separately, two outputs (`virus_info`, `tool_info`) have no in-memory getter at all; they exist only as CSV.

**Proposal.** Split *what to extract* from *where it goes*. All of it is additive, and the existing `std::function<void(size_t, Model<TSeq>*)>` overload of `run_multiple` stays unchanged.

1. **`SaveOptions`** is a struct with named bool fields (`total_hist`, `virus_info`, `virus_hist`, `tool_info`, `tool_hist`, `transmission`, `transition`, `reproductive`, `generation`, `active_cases`, `outbreak_size`, `hospitalizations`).
   - It replaces the 12 positional bools.
   - `make_save_run(fmt, b1..b12)` stays as a thin wrapper.
2. **`RunOutputs`** holds in-memory tables for one simulation, one struct per output.
   - Each table is stored column by column (`std::vector<int> date; std::vector<std::string> state; std::vector<int> counts; …`).
   - Each table has **exactly the columns of the matching CSV** from `write_data`, so R, Python and JS can turn them into data frames or typed arrays without parsing text.
   - It is filled by a new `DataBase<TSeq>::get_run_outputs(const SaveOptions&) const`. That method reuses the existing getters (`get_hist_total`, `get_hist_transition_matrix`, `get_transmissions`, …) and adds the missing `virus_info`/`tool_info` extraction.
3. **`Saver<TSeq>`** is a strategy interface for "where it goes":
   ```cpp
   template<typename TSeq> class Saver {
   public:
       explicit Saver(SaveOptions opts);
       virtual ~Saver() = default;
       virtual void begin(size_t nexperiments) {}          // e.g. preallocate
       RunOutputs extract(size_t sim_id, const Model<TSeq>&) const; // thread-safe
       virtual void write(size_t sim_id, RunOutputs&& out) = 0;     // serialized
       virtual void end() {}
   };
   ```
   Built-in strategies:
   - **`SaverMemory<TSeq>`** keeps results per sim, indexed by `sim_id`, so the order doesn't depend on which thread finishes first. Its `results()` returns all sims concatenated, with a leading `sim_id` column. This is the one epiworldjs, epiworldR and epiworldpy would use.
   - **`SaverFiles<TSeq>(fmt, opts)`** writes the same CSVs as today, byte-for-byte; `make_save_run` becomes sugar for it.
   - **`SaverCallback<TSeq>(opts, std::function<void(size_t, RunOutputs&&)>)`** is a user-defined sink: stream to JS, send to a database, compute summaries on the fly and drop the raw data.
4. A **new `run_multiple(ndays, nexperiments, seed, Saver<TSeq>& saver, reset, verbose, nthreads)` overload.**
   - It calls `saver.begin()` and `saver.end()` around the runs.
   - Under OpenMP, `extract` runs in each thread *outside* the critical section, and only `write` runs inside `#pragma omp critical`. Today the whole callback is serialized.
   - Bindings that drive sims themselves (the epiworldjs worker pool, which runs a slice of `sim_ids` with seeds drawn up front) call `extract`/`write` directly after each `run()`.
5. The file-writing default of `run_multiple` stays for now, for backward compatibility. Document `SaverMemory` as the recommended path; switching the default can wait for a later release.

**Files:**
- `include/epiworld/model-bones.hpp`/`model-meat.hpp` (overload, `make_save_run` wrapper)
- a new `include/epiworld/saver-bones.hpp` + `saver-meat.hpp` (`SaveOptions`, `RunOutputs`, `Saver*`), included from `include/epiworld/epiworld.hpp`
- `database-bones.hpp`/`database-meat.hpp` (`get_run_outputs`)
- Then regenerate `./epiworld.hpp` with `make build/epiworld.hpp`, and bump the **minor** version (0.18.0 → 0.19.0, a new feature) in `include/epiworld/epiworld.hpp`.

**Tests:** about three end-to-end files, each with exactly one `EPIWORLD_TEST_CASE`, using `run_multiple`:
- **(a) Memory matches files.** `SaverMemory` gives the same tables as the CSVs from `SaverFiles` and the legacy `make_save_run` for the same seed, across all 12 outputs, on an SEIRCONN model.
- **(b) Thread-count invariance.** `SaverMemory` results are identical with `nthreads` = 1 and 4, ordered by `sim_id`.
- **(c) Callback hooks.** A `SaverCallback` receives every `sim_id` exactly once, and `begin`/`end` are each called once.

**Follow-up PRs (out of scope here):**
- epiworldR: `make_saver(..., in_memory = TRUE)` returns data frames straight from `SaverMemory`, with no temp directory.
- epiworldpy: return the results as numpy arrays or dicts.

## Upstream changes (small PRs)
1. **epiworld:** fix `include/epiworld/postsampling-meat.hpp:31` to compare as `uint64_t`, e.g. `static_cast<uint64_t>(population.size()) >= (uint64_t(1) << 32)`. Then regenerate `./epiworld.hpp` with `make build/epiworld.hpp`. It doesn't change results on 64-bit, so no version bump is needed. Fix any other wasm32 warnings the first `emcc -Wall` build turns up the same way.
2. **epiworld:** add `.github/workflows/wasm.yml`. It compiles one example and a few tests with `emcc` and runs them under Node, so epiworld stays WASM-clean without anyone having to remember to check.
3. **measles:** apply the same wasm32 fixes if the build turns any up.
4. ~~Confirm the measles repo's license.~~ It is MIT (in R's `MIT + file LICENSE` form, which GitHub reports as "Other"); `make update-measles` copies its `LICENSE.md` next to the vendored headers.

## Implementation order
0. **epiworld PR:** upstream change A (savers for `run_multiple`, [#288](https://github.com/UofUEpiBio/epiworld/issues/288)), plus the wasm32 fix (upstream change 1), on a branch off `master`. This PR lands before epiworldjs vendors the headers.
1. Scaffold the repo: vendoring Makefile, `.devcontainer` (emsdk), AGENTS.md (conventions copied from epiworld/epiworldpy).
2. Get a minimal `bindings.cpp` that runs SIRCONN and prints `hist_total` in Node.
3. Registry + core for all built-in models. Golden tests: native g++ vs. wasm, same specs, exact match.
4. Measles models in the registry.
5. Worker pool and `Epiworld` JS API. Node tests.
6. `<epiworld-model>` plus the SVG chart. Site playground on GitHub Pages.
7. CI (build, golden, Node and Playwright tests), npm publish workflow (tag → `npm publish`, served by jsDelivr/unpkg), please-bump. README with copy-paste snippets for HTML and Quarto.
8. Follow-ups, not in v1: step-by-step `model.step()` for animated runs, custom models written in JS, network visualisation, calibration/LFMCMC.

## Verification
- **Golden equivalence:** `cpp/golden.cpp` (native g++, in the container) and the wasm build (Node) run the same set of specs: every registry model, fixed seed, 1 and 10 sims. The JSON outputs (`hist_total`, transitions, transmissions) must match exactly. If float contraction causes differences, build both with `-ffp-contract=off`.
- **Worker-count invariance:** in a browser test, `nsims=20` with 1, 2 and 4 workers must give identical results.
- **Node unit tests** (`node --test`): every model in `listModels()` runs with its defaults, conserves population (states sum to `n` every day), and rejects an unknown parameter or out-of-range value with a JS `Error` that names it.
- **Playwright smoke test:** load `site/index.html`, wait for the chart's SVG paths, move a slider, check that a re-run happens, and check that the CSV download has the right columns. Then load the component from a different origin to exercise the Blob-worker path.
- **Manual:** open the GitHub Pages playground and the Quarto example. Check bundle size (target < 1 MB gzipped `.wasm`) and the time to run SEIRCONN with n=10k, 100 days, 50 sims.
