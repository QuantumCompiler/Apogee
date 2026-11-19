import { dirname, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { expect, test } from '@playwright/test'

/** The committed build, lib/design/main.html: this file is e2e/opens-from-disk.spec.ts. */
const built = pathToFileURL(resolve(dirname(fileURLToPath(import.meta.url)), '../../main.html')).href

test('Chromium opens lib/design/main.html from disk, the app mounts, and the console logs no error', async ({ page }) => {
  const errors: string[] = []
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(message.text())
  })
  page.on('pageerror', (error) => errors.push(error.message))
  const loaded: string[] = []
  page.on('request', (request) => loaded.push(request.url()))

  await page.goto(built)

  await expect(page.locator('#root > main[data-apogee-app]')).toBeVisible()
  await expect(page.getByRole('main', { name: 'Apogee' })).toBeVisible()
  await expect(page).toHaveTitle('Apogee')
  expect(errors).toEqual([])
  expect(loaded).toEqual([built])
})
