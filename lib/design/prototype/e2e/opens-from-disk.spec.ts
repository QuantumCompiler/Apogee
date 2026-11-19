import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { expect, test } from '@playwright/test'
import type { Page } from '@playwright/test'
import { tokensFile } from '../tooling/tokens.ts'

/** The prototype's root: this file is e2e/opens-from-disk.spec.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')

/** The committed build, lib/design/main.html. */
const built = pathToFileURL(resolve(root, '../main.html')).href

/** tokens.json, which every color on screen must come from, wherever the build step reads it. */
const tokens = JSON.parse(readFileSync(tokensFile(root), 'utf8')) as {
  colors: { themes: Record<string, { colors: Record<string, string> }> }
}

/** Every console error and page error, and every request the page makes but data: URIs (the inlined fonts, which load nothing). */
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

const monitor = (page: Page) => page.getByRole('dialog', { name: 'System monitor' })

test('a double-click opens lib/design/main.html from disk: the monitor over the shell, no console error, no network request', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(built)
  await expect(page).toHaveTitle('Apogee')
  await expect(page.getByRole('complementary', { name: 'Sidebar' })).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Places' })).toBeVisible()
  await expect(monitor(page)).toBeVisible()
  await expect(monitor(page)).toContainText('THIS MAC · APPLE M3 MAX · 128 GB')
  await expect(page.getByText('Sessions isn’t drawn here yet. Design item 1f ports it.')).toBeVisible()
  // The web fonts are inlined: they load from the page itself. The faces that
  // loaded are the two the window uses; none loads when no face exists.
  expect(
    await page.evaluate(async () => (await document.fonts.ready, [...new Set([...document.fonts].filter((face) => face.status === 'loaded').map((face) => face.family))].sort())),
  ).toEqual(['IBM Plex Mono', 'IBM Plex Sans'])
  expect(await page.evaluate(() => [...document.fonts].filter((face) => face.status === 'error').length)).toBe(0)
  expect(errors).toEqual([])
  expect(requests).toEqual([built])
})

for (const form of ['?start=monitor&still=true', '#start=monitor&still=true']) {
  test(`${form} shows the frozen monitor, its numbers the recorded system document's`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(built + form)
    const dialog = monitor(page)
    await expect(dialog.getByRole('heading', { name: 'System' })).toBeVisible()
    await expect(dialog).toContainText('updates every 2 s · ⌥⌘M')
    await expect(dialog).toContainText('THIS MAC · APPLE M3 MAX · 128 GB')
    await expect(dialog).toContainText('CPU2%')
    await expect(dialog).toContainText('Apple M3 Max')
    await expect(dialog).toContainText('models may use 107.5 GB')
    await expect(dialog).toContainText('26.0')
    await expect(dialog).toContainText('Apogee 3.8 MB')
    await expect(dialog).toContainText('other apps 26.0 GB')
    await expect(dialog).toContainText('free 102.0 GB')
    await expect(dialog).toContainText('None by this process -- a session holds its own.')
    await expect(dialog).toContainText('MODEL STORE · /Users/someone/.apogee/models')
    await expect(dialog).toContainText('240.0 GB')
    await expect(dialog).toContainText('4.5 TB free on the store’s volume')
    await expect(dialog.locator('[data-history] > span')).toHaveCount(3)
    await expect(page.getByRole('region', { name: 'System' })).toContainText('mem26.0/128.0 GB')
    // Still: nothing moves.
    const before = await dialog.innerText()
    await page.waitForTimeout(2500)
    expect(await dialog.innerText()).toBe(before)
    expect(errors).toEqual([])
    // A request never carries its #fragment: the page is the one file either way.
    expect(requests).toEqual([(built + form).replace(/#.*$/, '')])
  })
}

test('in the missing and config-typo modes the monitor says why it cannot read, in the Mac app\'s and the CLI\'s words', async ({ page }) => {
  await page.goto(`${built}?start=monitor&still=true&apogee=missing`)
  await expect(monitor(page)).toContainText('Apogee isn’t installed, so the system can’t be read.')
  await page.goto(`${built}#start=monitor&still=true&apogee=config-typo`)
  await expect(monitor(page)).toContainText("apogee system: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
})

test('each of the 11 places opens by click, and an unported one shows its placeholder', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=monitor&still=true`)
  const places: Array<[string, string, string]> = [
    ['Sessions', '⌘1', '1f'], ['Models', '⌘2', '1h'], ['Suites', '⌘3', '1p'], ['Tasks', '⌘4', '1p'], ['Code graph', '⌘5', '1o'], ['Video', '⌘6', '1k'],
    ['Voice', '⌘7', '1m'], ['Knowledge', '⌘8', '1o'], ['Training', '⌘9', '1j'], ['Library', '⌘0', '1n'], ['Review', '⇧⌘R', '1n'],
  ]
  for (const [title, key, item] of [...places].reverse()) {
    const button = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: `${title} ${key}` })
    await button.click()
    await expect(button).toHaveAttribute('aria-current', 'page')
    await expect(page.getByRole('heading', { name: title, level: 1 })).toBeVisible()
    await expect(page.getByText(`${title} isn’t drawn here yet. Design item ${item} ports it.`)).toBeVisible()
  }
  expect(errors).toEqual([])
})

test('⌥⌘M closes and opens the monitor; "Open models library" goes to Models', async ({ page }) => {
  await page.goto(`${built}?start=monitor&still=true`)
  await expect(monitor(page)).toBeVisible()
  await page.keyboard.press('Alt+Meta+KeyM')
  await expect(monitor(page)).toHaveCount(0)
  await page.keyboard.press('Alt+Meta+KeyM')
  await expect(monitor(page)).toBeVisible()
  await monitor(page).getByRole('button', { name: 'Open models library' }).click()
  await expect(monitor(page)).toHaveCount(0)
  await expect(page.getByRole('heading', { name: 'Models', level: 1 })).toBeVisible()
})

/** Every color the page paints: each element's text, background and border colors that are not transparent. */
async function paintedColors(page: Page): Promise<string[]> {
  return page.evaluate(() => {
    const seen = new Set<string>()
    for (const element of document.querySelectorAll('#root *')) {
      const style = getComputedStyle(element)
      if (style.display === 'none' || style.visibility === 'hidden') continue
      for (const value of [style.color, style.backgroundColor, style.borderTopColor, style.borderBottomColor, style.borderLeftColor, style.borderRightColor]) {
        if (value !== 'rgba(0, 0, 0, 0)') seen.add(value)
      }
    }
    return [...seen]
  })
}

/** `#131415` as the browser reports it: `rgb(19, 20, 21)`. */
const rgb = (hex: string) => `rgb(${parseInt(hex.slice(1, 3), 16)}, ${parseInt(hex.slice(3, 5), 16)}, ${parseInt(hex.slice(5, 7), 16)})`

test("the sidebar's theme button re-colors the whole window in all 3 themes: every color painted is the theme's", async ({ page }) => {
  await page.goto(`${built}?start=monitor&still=true`)
  const window = page.locator('#root > .ap')
  const button = page.getByRole('button', { name: /^Theme: / })
  const fixed = ['#FF5F57', '#FEBC2E', '#28C840'].map(rgb)
  for (const [theme, next] of [['dark', 'Graphite Light'], ['light', 'High Contrast'], ['contrast', 'Automatic']] as const) {
    await expect(window).toHaveAttribute('data-theme', theme)
    const palette = new Set([...Object.values(tokens.colors.themes[theme]?.colors ?? {}).map(rgb), ...fixed])
    const outside = (await paintedColors(page)).filter((color) => !palette.has(color))
    expect(outside, `${theme}: colors not in the theme`).toEqual([])
    await expect(window).toHaveCSS('background-color', rgb(tokens.colors.themes[theme]?.colors['surface.window'] ?? ''))
    await button.click()
    await expect(page.getByRole('status')).toHaveText(`Theme: ${next}`)
  }
  await expect(window).toHaveAttribute('data-theme', 'dark')
})

test('the type sizes are the tokens\', scaled by the text size: the window carries every --t-* size at medium', async ({ page }) => {
  await page.goto(`${built}?start=monitor&still=true`)
  const sizes = await page.locator('#root > .ap').evaluate((element) => {
    const style = getComputedStyle(element)
    return ['display', 'body', 'meta', 'tag'].map((step) => style.getPropertyValue(`--t-${step}`).trim())
  })
  expect(sizes).toEqual(['24px', '13px', '11px', '9.5px'])
})
