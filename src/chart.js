/**
 * A dependency-free SVG chart of state counts over time: one median line per
 * state and, with more than one simulation, a band for the central interval.
 *
 * Colors come from CSS custom properties, `--epiworld-color-<state>` (the
 * label in lower case, with runs of other characters turned into "-"), and
 * fall back to a built-in palette.
 */

const SVG = "http://www.w3.org/2000/svg";

// Okabe-Ito, which stays distinguishable with common color-vision deficiencies
// (without its black, which disappears on dark backgrounds), then a few more
const PALETTE = ["#0072b2", "#e69f00", "#d55e00", "#009e73", "#cc79a7", "#56b4e9", "#f0e442",
  "#8c8c8c", "#a6761d", "#6a3d9a", "#b2df8a", "#fb9a99"];

/** "Quarantined Exposed" → "quarantined-exposed" */
export function stateSlug(state) {
  return state.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
}

export function stateColor(state, i) {
  return `var(--epiworld-color-${stateSlug(state)}, ${PALETTE[i % PALETTE.length]})`;
}

function el(name, attrs = {}, parent) {
  const node = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
  parent?.appendChild(node);
  return node;
}

/** About `count` round tick values from 0 up to the first one >= max. */
function ticks(max, count = 5) {
  if (max <= 0) return [0];
  const raw = max / count;
  const magnitude = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * magnitude).find((s) => s >= raw);
  const out = [0];
  while (out[out.length - 1] < max - step * 1e-9) out.push(+(out.length * step).toFixed(10));
  return out;
}

const format = new Intl.NumberFormat(undefined, { maximumFractionDigits: 1 });

/**
 * Draws (or redraws) the chart into `container`.
 *
 * @param {HTMLElement} container Gets an <svg>, a legend and a tooltip.
 * @param {ReturnType<import("./result.js").Result["summary"]>} summary
 * @param {{bands?: boolean, hidden?: Set<string>, onToggle?: (state: string) => void}} [options]
 */
export function renderChart(container, summary, { bands = true, hidden = new Set(), onToggle } = {}) {
  container.replaceChildren();

  const W = 640, H = 320, M = { top: 12, right: 16, bottom: 36, left: 56 };
  const { dates, states } = summary;
  const visible = states.filter((s) => !hidden.has(s));
  // Many overlapping bands are unreadable; then only the medians are drawn
  bands = bands && visible.length <= 6;
  const lastDay = dates[dates.length - 1] || 1;

  let ymax = 0;
  for (const s of visible)
    for (const v of (bands ? summary.upper : summary.median)[s]) ymax = Math.max(ymax, v);
  const yt = ticks(ymax || 1);
  const ytop = yt[yt.length - 1];

  const x = (d) => M.left + (d / lastDay) * (W - M.left - M.right);
  const y = (v) => H - M.bottom - (v / ytop) * (H - M.top - M.bottom);

  const svg = el("svg", { viewBox: `0 0 ${W} ${H}`, role: "img", part: "chart-svg" });
  svg.appendChild(Object.assign(el("title"), { textContent: "Agents in each state per day" }));

  // Grid and axes
  const axes = el("g", { class: "axes" }, svg);
  for (const v of yt) {
    el("line", { x1: M.left, x2: W - M.right, y1: y(v), y2: y(v), class: "grid" }, axes);
    el("text", { x: M.left - 8, y: y(v), class: "tick y" }, axes).textContent = format.format(v);
  }
  for (const d of ticks(lastDay, 6).filter((d) => Number.isInteger(d) && d <= lastDay)) {
    el("line", { x1: x(d), x2: x(d), y1: H - M.bottom, y2: H - M.bottom + 4, class: "axis" }, axes);
    el("text", { x: x(d), y: H - M.bottom + 16, class: "tick x" }, axes).textContent = d;
  }
  el("line", { x1: M.left, x2: W - M.right, y1: H - M.bottom, y2: H - M.bottom, class: "axis" }, axes);
  el("text", { x: (M.left + W - M.right) / 2, y: H - 4, class: "label" }, axes).textContent = "Day";

  // Bands under all lines, then lines
  const series = el("g", { class: "series" }, svg);
  const path = (values) => Array.from(values, (v, i) => `${i ? "L" : "M"}${x(dates[i]).toFixed(1)},${y(v).toFixed(1)}`).join("");
  states.forEach((s, i) => {
    if (hidden.has(s) || !bands) return;
    const upper = path(summary.upper[s]);
    const lower = Array.from(summary.lower[s], (v, j) => [x(dates[j]).toFixed(1), y(v).toFixed(1)])
      .reverse().map(([a, b]) => `L${a},${b}`).join("");
    el("path", { d: `${upper}${lower}Z`, class: "band", fill: stateColor(s, i), "data-state": s }, series);
  });
  states.forEach((s, i) => {
    if (hidden.has(s)) return;
    el("path", { d: path(summary.median[s]), class: "line", stroke: stateColor(s, i), "data-state": s }, series);
  });

  // Hover readout: a rule at the nearest day and the values in the tooltip
  const rule = el("line", { y1: M.top, y2: H - M.bottom, class: "rule", visibility: "hidden" }, svg);
  const overlay = el("rect", { x: M.left, y: M.top, width: W - M.left - M.right, height: H - M.top - M.bottom, fill: "transparent" }, svg);
  const tooltip = document.createElement("div");
  tooltip.className = "tooltip";
  tooltip.setAttribute("part", "tooltip");
  tooltip.hidden = true;

  overlay.addEventListener("pointermove", (event) => {
    const box = svg.getBoundingClientRect();
    const px = ((event.clientX - box.left) / box.width) * W;
    const day = Math.max(0, Math.min(lastDay, Math.round(((px - M.left) / (W - M.left - M.right)) * lastDay)));
    rule.setAttribute("x1", x(day));
    rule.setAttribute("x2", x(day));
    rule.setAttribute("visibility", "visible");
    tooltip.replaceChildren(Object.assign(document.createElement("strong"), { textContent: `Day ${day}` }));
    visible.forEach((s) => {
      const row = document.createElement("div");
      const swatch = Object.assign(document.createElement("span"), { className: "swatch" });
      swatch.style.background = stateColor(s, states.indexOf(s));
      const range = bands ? ` (${format.format(summary.lower[s][day])}–${format.format(summary.upper[s][day])})` : "";
      row.append(swatch, `${s}: ${format.format(summary.median[s][day])}${range}`);
      tooltip.append(row);
    });
    tooltip.hidden = false;
    const left = (x(day) / W) * box.width;
    tooltip.style.left = `${left}px`;
    tooltip.style.transform = left > box.width / 2 ? "translateX(calc(-100% - 12px))" : "translateX(12px)";
  });
  overlay.addEventListener("pointerleave", () => {
    rule.setAttribute("visibility", "hidden");
    tooltip.hidden = true;
  });

  // Legend; clicking a state hides or shows it
  const legend = document.createElement("div");
  legend.className = "legend";
  legend.setAttribute("part", "legend");
  states.forEach((s, i) => {
    const button = document.createElement("button");
    button.type = "button";
    button.setAttribute("aria-pressed", String(!hidden.has(s)));
    const swatch = Object.assign(document.createElement("span"), { className: "swatch" });
    swatch.style.background = stateColor(s, i);
    button.append(swatch, s);
    button.addEventListener("click", () => onToggle?.(s));
    legend.append(button);
  });

  const plot = document.createElement("div");
  plot.className = "plot";
  plot.append(svg, tooltip);
  container.append(legend, plot);
}

/** CSS for the chart, for the shadow root that hosts it. */
export const chartStyles = `
  .plot { position: relative; }
  svg { display: block; width: 100%; height: auto; overflow: visible; }
  .grid { stroke: var(--epiworld-grid, color-mix(in srgb, currentColor 12%, transparent)); }
  .axis { stroke: var(--epiworld-axis, color-mix(in srgb, currentColor 45%, transparent)); }
  .tick, .label { fill: currentColor; font-size: 11px; opacity: .75; }
  .tick.y { text-anchor: end; dominant-baseline: middle; }
  .tick.x, .label { text-anchor: middle; }
  .line { fill: none; stroke-width: 2; stroke-linejoin: round; }
  .band { opacity: .18; stroke: none; }
  .rule { stroke: currentColor; stroke-opacity: .35; stroke-dasharray: 3 3; }
  .legend { display: flex; flex-wrap: wrap; gap: 4px 12px; margin-bottom: 6px; }
  .legend button { all: unset; cursor: pointer; display: inline-flex; align-items: center; gap: 6px; font-size: 13px; }
  .legend button[aria-pressed="false"] { opacity: .4; text-decoration: line-through; }
  .legend button:focus-visible { outline: 2px solid currentColor; outline-offset: 2px; }
  .swatch { width: 10px; height: 10px; border-radius: 2px; flex: none; display: inline-block; }
  .tooltip { position: absolute; top: 8px; pointer-events: none; font-size: 12px; line-height: 1.5;
    background: var(--epiworld-surface, Canvas); color: var(--epiworld-text, CanvasText);
    border: 1px solid color-mix(in srgb, currentColor 20%, transparent); border-radius: 6px;
    padding: 6px 8px; white-space: nowrap; box-shadow: 0 2px 8px rgb(0 0 0 / .12); }
  .tooltip .swatch { margin-right: 6px; }
`;
