import { test, expect } from "@playwright/test";
import { serve } from "./serve.mjs";

let site, cdn;
test.beforeAll(async () => {
  site = await serve(0);
  cdn = await serve(0);
});
test.afterAll(() => {
  site.close();
  cdn.close();
});

const url = (server, path) => `http://127.0.0.1:${server.address().port}${path}`;

// The playground's state: summary text and number of runs so far
async function playground(page) {
  const el = page.locator("#playground-model");
  await expect(el.locator("path.line").first()).toBeVisible({ timeout: 30_000 });
  return el;
}

test("the playground renders, re-runs when a slider moves, and switches models", async ({ page }) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(url(site, "/site/"));
  const el = await playground(page);

  await expect(el.locator(".legend button")).toHaveText(["Susceptible", "Exposed", "Infected", "Recovered"]);
  await expect(el.locator(".summary")).toContainText("Peak active cases");
  const before = await el.locator(".summary").textContent();

  // Count runs from here on
  await page.evaluate(() => {
    window.runs = 0;
    document.querySelector("#playground-model").addEventListener("epiworld-result", () => window.runs++);
  });
  const slider = el.locator("label", { hasText: "Contact rate" }).locator("input");
  await slider.fill("10");
  await expect.poll(() => page.evaluate(() => window.runs)).toBeGreaterThan(0);
  await expect(el.locator(".summary")).not.toHaveText(before);

  // The picker rebuilds the controls and the legend for the new model
  await el.locator("select").selectOption("SIRD");
  await expect(el.locator(".legend button")).toHaveText(["Susceptible", "Infected", "Recovered", "Deceased"]);
  await expect(el.locator("label", { hasText: "Death rate" })).toBeVisible();

  expect(errors).toEqual([]);
});

test("Download CSV saves epiworld's total_hist table", async ({ page }) => {
  await page.goto(url(site, "/site/"));
  const el = await playground(page);
  const [download] = await Promise.all([
    page.waitForEvent("download"),
    el.getByRole("button", { name: "Download CSV" }).click(),
  ]);
  expect(download.suggestedFilename()).toBe("SEIRCONN.csv");
  const csv = await (await download.createReadStream()).toArray();
  const lines = Buffer.concat(csv).toString().trimEnd().split("\n");
  expect(lines[0]).toBe("sim_id,date,nviruses,state,counts");
  // 20 simulations x 101 days x 4 states
  expect(lines.length).toBe(1 + 20 * 101 * 4);
});

test("loads from another origin, as from a CDN", async ({ page }) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  const src = url(cdn, "/dist/epiworld-model.js");
  await page.goto(url(site, `/test/e2e/cross-origin.html?src=${encodeURIComponent(src)}`));
  const el = page.locator("epiworld-model");
  await expect(el.locator("path.line").first()).toBeVisible({ timeout: 30_000 });
  await expect(el.locator(".status")).toContainText("8 simulations");
  // The workers really are separate threads loading the module cross-origin
  expect(await page.evaluate(async () => {
    const { Epiworld } = await import(new URLSearchParams(location.search).get("src"));
    const ew = await Epiworld.load({ workers: 2 });
    const workers = ew.workers;
    const res = await ew.run({ model: "SIR", n: 300, ndays: 5, nsims: 4 });
    ew.terminate();
    return [workers, res.tables.total_hist.sim_id.length];
  })).toEqual([2, 4 * 6 * 3]);
  expect(errors).toEqual([]);
});
