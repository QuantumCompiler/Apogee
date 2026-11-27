import { defineConfig, devices } from '@playwright/test'

// One smoke test, Chromium only: the built lib/design/main.html opened from
// disk, as a double-click opens it. No server: the page is a file:// URL.
export default defineConfig({
  testDir: 'e2e',
  forbidOnly: true,
  reporter: 'list',
  outputDir: 'test-results',
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
})
