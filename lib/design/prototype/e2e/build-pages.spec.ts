import { dirname, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { expect, test } from '@playwright/test'
import type { Page } from '@playwright/test'

/** The committed build, lib/design/main.html: this file is e2e/build-pages.spec.ts. */
const built = pathToFileURL(resolve(dirname(fileURLToPath(import.meta.url)), '../../main.html')).href

/** Every console error and page error, and every request the page makes but data: URIs (the inlined fonts and logo, which load nothing). */
function watch(page: Page) {
  const errors: string[] = []
  const requests: string[] = []
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(message.text())
  })
  page.on('pageerror', (error) => errors.push(error.message))
  page.on('request', (request) => {
    if (!request.url().startsWith('data:')) requests.push(request.url())
  })
  return { errors, requests }
}

/** A Build page's address: the gallery's focused board (1q's form, which 1r takes). */
const address = (board: string) => `${built}?gallery&board=${board}`

test('the contract page opens from disk at its address: its stat cards counted, its parts, a gap chip bringing its gap into view; no console error, no request but the file (design 1r)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(address('BuildContract'))
  const board = page.getByRole('region', { name: 'Build · how the app talks to Apogee' })
  await expect(board.getByRole('heading', { name: 'How the app talks to Apogee', exact: true })).toBeVisible()
  for (const [status, count] of [
    ['ready', '87'],
    ['app', '13'],
    ['gap', '16'],
    ['verify', '0'],
  ] as const) {
    await expect(board.locator(`[data-card="${status}"]`)).toContainText(count)
  }
  for (const part of ['SESSIONS', 'MODELS', 'SETTINGS AND SYSTEM']) await expect(board.getByRole('heading', { name: part, exact: true })).toBeAttached()
  const gap = board.locator('[id="contract-gap-G25"]')
  await expect(gap).not.toBeInViewport()
  await board.getByRole('link', { name: 'gap G25' }).click()
  await expect(gap).toBeInViewport()
  await expect(gap).toContainText('The GPU’s use isn’t read')
  await expect(page).toHaveURL(address('BuildContract'))
  expect(errors).toEqual([])
  expect(requests).toEqual([address('BuildContract')])
})

test('the layout page opens from disk at its address: SWIFTUI · MACOS 27, its sizes and keys; no console error, no request but the file (design 1r)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(address('BuildLayout'))
  const board = page.getByRole('region', { name: 'Build · layout, navigation and native controls' })
  await expect(board.getByText('SWIFTUI · MACOS 27')).toBeVisible()
  await expect(board.getByText('MACOS 14')).toHaveCount(0)
  await expect(board.getByText('Places · 40 × 34 each')).toBeVisible()
  await expect(board.getByText('380 · 340', { exact: true })).toBeVisible()
  await expect(board.getByText('⌘N · ⇧⌘N · ⌥⌘N', { exact: true })).toBeVisible()
  // The window at three quarters: the sidebar's box 192 px wide.
  expect(await board.locator('[data-part="sidebar"]').evaluate((element) => element.getBoundingClientRect().width)).toBe(192)
  expect(errors).toEqual([])
  expect(requests).toEqual([address('BuildLayout')])
})

test('the processes page opens from disk at its address, follows its board’s theme, and draws no hex; no console error, no request but the file (design 1r)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(address('BuildProcesses'))
  const board = page.getByRole('region', { name: 'Build · how the app runs Apogee' })
  await expect(board.getByRole('heading', { name: 'How the app runs Apogee', exact: true })).toBeVisible()
  await expect(board.getByText('stores re-read from the CLI')).toBeVisible()
  const surface = () => board.locator('[data-build-page="processes"]').evaluate((element) => getComputedStyle(element).backgroundColor)
  const dark = await surface()
  await page.getByRole('group', { name: 'Theme' }).getByRole('button', { name: 'Graphite Light' }).click()
  await expect.poll(surface).not.toBe(dark)
  expect(await board.locator('[data-build-page="processes"]').evaluate((element) => /#[0-9a-f]{3,8}\b/i.test(element.outerHTML))).toBe(false)
  expect(errors).toEqual([])
  expect(requests).toEqual([address('BuildProcesses')])
})
