/**
 * <epiworld-model>: an epiworld model with controls and a chart, in one tag.
 *
 *   <epiworld-model model="SEIRCONN" ndays="100" nsims="30" n="10000"
 *     params='{"Contact rate": 4}' controls="Contact rate, Prob. Transmission"
 *     autorun live></epiworld-model>
 *
 * Attributes
 *   model         Model id (default "SIRCONN"); see Epiworld.models().
 *   n, ndays, nsims, seed, prevalence
 *                 As in the run spec.
 *   params        JSON object of parameter values, by epiworld name.
 *   controls      Comma-separated names to show as sliders: parameters, and
 *                 "n", "ndays", "nsims", "prevalence", "seed"; or "all".
 *   model-picker  Shows a drop-down with every model.
 *   autorun       Runs once loaded.
 *   live          Re-runs (debounced) while the controls change.
 *   hide          Comma-separated states hidden in the chart at first (the
 *                 legend shows them again).
 *
 * Properties and events
 *   .result       The last Result (see result.js).
 *   .spec         The spec of the next run, from the attributes and controls.
 *   run()         Runs now; resolves to the Result.
 *   "epiworld-result" event, with the Result as `detail`.
 *
 * Styling: CSS custom properties (--epiworld-color-<state>, --epiworld-accent,
 * --epiworld-font, --epiworld-surface, --epiworld-text) and ::part(controls,
 * chart, summary, run, download, picker, error, legend, tooltip).
 */

import { Epiworld } from "./index.js";
import { renderChart, chartStyles } from "./chart.js";

// One worker pool for every element on the page
let epiworldPromise;
function epiworld() {
  epiworldPromise ??= Epiworld.load().catch((error) => {
    epiworldPromise = undefined;
    throw error;
  });
  return epiworldPromise;
}

// Spec fields that can be controls, with their slider ranges
const GENERAL = {
  n: { label: "Population size", min: 100, max: 100000, step: 100, integer: true },
  ndays: { label: "Days", min: 1, max: 365, step: 1, integer: true },
  nsims: { label: "Simulations", min: 1, max: 200, step: 1, integer: true },
  prevalence: { label: "Initial prevalence", min: 0, max: 0.2, step: 0.001, integer: false },
  seed: { label: "Seed", min: 0, max: 9999, step: 1, integer: true },
};

const OUTPUTS = ["total_hist", "active_cases", "outbreak_size"];

const format = new Intl.NumberFormat(undefined, { maximumFractionDigits: 3 });
const whole = new Intl.NumberFormat(undefined, { maximumFractionDigits: 0 });

const styles = `
  :host {
    display: block;
    font-family: var(--epiworld-font, system-ui, sans-serif);
    color: var(--epiworld-text, CanvasText);
    background: var(--epiworld-surface, Canvas);
    color-scheme: light dark;
    border: 1px solid color-mix(in srgb, currentColor 15%, transparent);
    border-radius: 10px;
    padding: 16px;
  }
  :host([hidden]) { display: none; }
  .layout { display: grid; gap: 20px; grid-template-columns: minmax(0, 260px) minmax(0, 1fr); }
  @container (max-width: 600px) { .layout { grid-template-columns: 1fr; } }
  .wrap { container-type: inline-size; }
  .controls { display: grid; gap: 10px; align-content: start; }
  .controls:empty { display: none; }
  label { display: grid; gap: 2px; font-size: 13px; min-width: 0; }
  .name { display: flex; justify-content: space-between; gap: 8px; }
  .name output { font-variant-numeric: tabular-nums; opacity: .8; }
  input[type=range] { width: 100%; accent-color: var(--epiworld-accent, #0072b2); margin: 0; }
  select { font: inherit; padding: 4px; width: 100%; min-width: 0; }
  .buttons { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 4px; }
  button.action { font: inherit; font-size: 14px; padding: 6px 14px; border-radius: 6px; cursor: pointer;
    border: 1px solid var(--epiworld-accent, #0072b2); background: var(--epiworld-accent, #0072b2); color: white; }
  button.action.secondary { background: transparent; color: var(--epiworld-accent, #0072b2); }
  button.action:disabled { opacity: .5; cursor: progress; }
  .summary { display: flex; flex-wrap: wrap; gap: 4px 20px; font-size: 13px; margin-top: 8px; }
  .summary b { font-variant-numeric: tabular-nums; }
  .status { font-size: 13px; opacity: .7; min-height: 1.5em; }
  .error { color: #d55e00; font-size: 13px; white-space: pre-wrap; }
  .error:empty { display: none; }
  ${chartStyles}
`;

function parseList(value) {
  return (value ?? "").split(",").map((s) => s.trim()).filter(Boolean);
}

export class EpiworldModelElement extends HTMLElement {
  static observedAttributes = ["model", "n", "ndays", "nsims", "seed", "prevalence", "params", "controls", "model-picker"];

  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this.shadowRoot.innerHTML = `
      <style>${styles}</style>
      <div class="wrap">
        <div class="layout">
          <div class="controls" part="controls"></div>
          <div>
            <div class="chart" part="chart"></div>
            <div class="summary" part="summary"></div>
            <div class="status" role="status"></div>
            <div class="error" part="error" role="alert"></div>
          </div>
        </div>
      </div>`;
    this._controls = this.shadowRoot.querySelector(".controls");
    this._chart = this.shadowRoot.querySelector(".chart");
    this._summary = this.shadowRoot.querySelector(".summary");
    this._status = this.shadowRoot.querySelector(".status");
    this._error = this.shadowRoot.querySelector(".error");
    this._values = {};       // Values set through the controls
    this._hidden = new Set(); // States hidden in the chart
    this._runs = 0;
    this.result = undefined;
  }

  async connectedCallback() {
    this._status.textContent = "Loading…";
    try {
      this._ew = await epiworld();
    } catch (error) {
      this._showError(error);
      return;
    }
    this._status.textContent = "";
    this._hidden = new Set(parseList(this.getAttribute("hide")));
    this._build();
    if (this.hasAttribute("autorun")) this.run();
  }

  attributeChangedCallback(name, previous, value) {
    if (!this._ew || previous === value) return;
    if (name === "model") this._values = {};
    this._build();
    if (this.hasAttribute("live")) this._scheduleRun();
  }

  get model() {
    const id = this._values.model ?? this.getAttribute("model") ?? "SIRCONN";
    const model = this._ew?.models().find((m) => m.id === id);
    if (this._ew && !model) throw new Error(`Unknown model "${id}".`);
    return model;
  }

  /** The spec of the next run: attributes, then values set in the controls. */
  get spec() {
    const model = this.model;
    const spec = { model: model.id, outputs: OUTPUTS };
    for (const key of Object.keys(GENERAL)) {
      const value = this._values[key] ?? this.getAttribute(key);
      if (value != null && value !== "") spec[key] = Number(value);
    }

    let params = {};
    if (this.hasAttribute("params")) {
      try {
        params = JSON.parse(this.getAttribute("params"));
      } catch {
        throw new Error("The params attribute must be a JSON object.");
      }
    }
    // Attribute params are kept when the picker switches to a model that has them
    const names = new Set(model.params.map((p) => p.name));
    spec.params = Object.fromEntries(Object.entries(params).filter(([k]) => names.has(k) || this._values.model == null));
    for (const p of model.params)
      if (this._values[p.name] != null) spec.params[p.name] = this._values[p.name];
    return spec;
  }

  _controlNames(model) {
    const controls = this.getAttribute("controls");
    if (controls === "all" || (controls == null && this.hasAttribute("model-picker")))
      return ["ndays", "nsims", "n", "prevalence", ...model.params.map((p) => p.name)];
    return parseList(controls);
  }

  _build() {
    let model;
    try {
      model = this.model;
    } catch (error) {
      this._showError(error);
      return;
    }
    this._controls.replaceChildren();
    const spec = this.spec;

    if (this.hasAttribute("model-picker")) {
      const label = document.createElement("label");
      label.innerHTML = `<span class="name">Model</span>`;
      const select = document.createElement("select");
      select.setAttribute("part", "picker");
      const families = new Map();
      for (const m of this._ew.models()) {
        if (!families.has(m.family)) {
          const group = document.createElement("optgroup");
          group.label = m.family[0].toUpperCase() + m.family.slice(1);
          families.set(m.family, group);
          select.append(group);
        }
        families.get(m.family).append(new Option(m.label, m.id, false, m.id === model.id));
      }
      select.addEventListener("change", () => {
        this._values = { model: select.value };
        this._hidden = new Set(parseList(this.getAttribute("hide")));
        this._build();
        this.run();
      });
      label.append(select);
      this._controls.append(label);
    }

    for (const name of this._controlNames(model)) {
      const param = model.params.find((p) => p.name === name);
      const info = param ?? GENERAL[name];
      if (!info) {
        this._showError(new Error(`Unknown control "${name}" for model ${model.id}.`));
        continue;
      }
      const value = param
        ? (spec.params[name] ?? param.value)
        : (spec[name] ?? this._default(name, model));
      this._controls.append(this._slider(name, param?.name ?? info.label, info, value, param?.description));
    }

    const buttons = document.createElement("div");
    buttons.className = "buttons";
    const run = Object.assign(document.createElement("button"), { type: "button", className: "action", textContent: "Run" });
    run.setAttribute("part", "run");
    run.addEventListener("click", () => this.run());
    const download = Object.assign(document.createElement("button"),
      { type: "button", className: "action secondary", textContent: "Download CSV", disabled: !this.result });
    download.setAttribute("part", "download");
    download.addEventListener("click", () => this._download());
    buttons.append(run, download);
    this._controls.append(buttons);
    this._runButton = run;
    this._downloadButton = download;
  }

  // What the core uses when a spec field is unset
  _default(name, model) {
    return { ndays: 100, nsims: 1, seed: 1, n: model.n, prevalence: model.prevalence }[name];
  }

  _slider(key, label, info, value, description) {
    const wrapper = document.createElement("label");
    const name = document.createElement("span");
    name.className = "name";
    const output = document.createElement("output");
    output.textContent = format.format(value);
    name.append(label, output);
    if (description) wrapper.title = description;

    const input = document.createElement("input");
    input.type = "range";
    // Sliders span the limits, widened if the current value is outside them
    input.min = Math.min(info.min, value);
    input.max = Math.max(info.max, value);
    input.step = info.step;
    input.value = value;
    input.addEventListener("input", () => {
      const v = Number(input.value);
      output.textContent = format.format(v);
      this._values[key] = v;
      if (this.hasAttribute("live")) this._scheduleRun();
    });
    wrapper.append(name, input);
    return wrapper;
  }

  _scheduleRun() {
    clearTimeout(this._timer);
    this._timer = setTimeout(() => this.run(), 250);
  }

  /** Runs the model with the current spec. */
  async run() {
    if (!this._ew) return undefined;
    const id = ++this._runs;
    this._error.textContent = "";
    if (this._runButton) this._runButton.disabled = true;
    this._status.textContent = "Running…";
    const started = performance.now();
    try {
      const result = await this._ew.run(this.spec);
      // A later run started meanwhile; its result wins
      if (id !== this._runs) return result;
      this.result = result;
      this._render();
      const seconds = ((performance.now() - started) / 1000).toFixed(2);
      this._status.textContent = `${result.nsims} simulation${result.nsims > 1 ? "s" : ""} in ${seconds} s`;
      this.dispatchEvent(new CustomEvent("epiworld-result", { detail: result, bubbles: true, composed: true }));
      return result;
    } catch (error) {
      if (id === this._runs) this._showError(error);
      throw error;
    } finally {
      if (id === this._runs && this._runButton) this._runButton.disabled = false;
    }
  }

  _render() {
    const result = this.result;
    const draw = () => renderChart(this._chart, result.summary(), {
      bands: result.nsims > 1,
      hidden: this._hidden,
      onToggle: (state) => {
        if (this._hidden.has(state)) this._hidden.delete(state);
        else this._hidden.add(state);
        draw();
      },
    });
    draw();

    const o = result.outbreak();
    const range = (x) => (result.nsims > 1 ? ` <span>(${whole.format(x.lower)}–${whole.format(x.upper)})</span>` : "");
    this._summary.innerHTML = `
      <span>Peak active cases: <b>${whole.format(o.peak.median)}</b>${range(o.peak)}</span>
      <span>Day of peak: <b>${whole.format(o.peakDay.median)}</b>${range(o.peakDay)}</span>
      <span>Outbreak size: <b>${whole.format(o.finalSize.median)}</b>${range(o.finalSize)}</span>`;
    if (this._downloadButton) this._downloadButton.disabled = false;
  }

  _download() {
    if (!this.result) return;
    const blob = new Blob([this.result.toCSV()], { type: "text/csv" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `${this.result.spec.model}.csv`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }

  _showError(error) {
    this._status.textContent = "";
    this._error.textContent = error?.message ?? String(error);
  }
}

if (!customElements.get("epiworld-model"))
  customElements.define("epiworld-model", EpiworldModelElement);
