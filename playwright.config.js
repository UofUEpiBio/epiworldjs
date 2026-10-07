import { defineConfig } from "@playwright/test";

// PW_CHANNEL=chrome uses an installed Google Chrome instead of Playwright's
// own Chromium (which `npx playwright install chromium` downloads).
export default defineConfig({
  testDir: "test/e2e",
  timeout: 60_000,
  use: { channel: process.env.PW_CHANNEL || undefined },
});
