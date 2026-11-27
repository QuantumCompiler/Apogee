import { dirname, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { expect, test } from '@playwright/test'
import type { Page } from '@playwright/test'

/** The committed build, lib/design/main.html: this file is e2e/gallery.spec.ts. */
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

const tab = (page: Page, name: string) => page.getByRole('tab', { name })
const heading = (page: Page, name: string) => page.getByRole('heading', { name, exact: true })

test('?gallery opens from disk on the Build page: the canvas’s five headings over their rows, no console error, no request but the file (design 1q)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(`${built}?gallery`)
  await expect(heading(page, 'Gallery')).toBeVisible()
  await expect(tab(page, 'Build')).toHaveAttribute('aria-selected', 'true')
  for (const text of [
    'Build — the contract, the measurements, and the app itself',
    'Screens — sessions, the system monitor and the console',
    'Screens — models, suites, tasks, the code graph, knowledge, training, the library and review',
    'Screens — video and voice',
    'Screens — settings',
  ]) {
    await expect(heading(page, text)).toBeAttached()
  }
  await expect(page.getByRole('button', { name: 'Apogee · the whole app (interactive)' })).toBeVisible()
  await expect(page.getByRole('button', { name: 'Session · streaming, thinking open' })).toBeVisible()
  // A board-less preset's frame, at the end of its row, marked.
  await expect(page.getByRole('button', { name: 'Settings · Keyboard NO OLD BOARD' })).toBeAttached()
  // The Build row's Contract, Layout and Processes draw their pages (1r).
  await expect(page.locator('[data-board="BuildContract"]')).toContainText('How the app talks to Apogee')
  await expect(page.locator('[data-board="BuildLayout"]')).toContainText('Layout, navigation and native controls')
  await expect(page.locator('[data-board="BuildProcesses"]')).toContainText('How the app runs Apogee')
  // The first rows' frames are the real app, mounted: the Streaming session.
  await expect(page.locator('[data-board="ScreenSessionStreaming"]')).toContainText('Where do fetches start in this repo, and what cancels them?')
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?gallery`])
})

test('?gallery=foundations opens from disk on the Foundations page: the color, type, four components and logo boards, no console error, no request but the file (design 1q)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(`${built}?gallery=foundations`)
  await expect(tab(page, 'Foundations')).toHaveAttribute('aria-selected', 'true')
  await expect(heading(page, 'Foundations — color, type, space and components')).toBeVisible()
  for (const title of ['Foundations · Color tokens', 'Foundations · Type, space, shape', 'Components · Model, context, sources', 'Components · The turn', 'Components · Asking and failing', 'Components · Making media', 'Foundations · The logo']) {
    await expect(page.getByRole('button', { name: title })).toBeVisible()
  }
  await page.locator('[data-board="Logo"]').scrollIntoViewIfNeeded()
  await expect(page.locator('[data-board="Logo"] img').first()).toHaveAttribute('src', /^data:image\/png;base64,/)
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?gallery=foundations`])
})

test('a screen board clicked opens at full size, still, its address with it; Esc goes back to the rows (design 1q)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?gallery`)
  await page.locator('[data-board="ScreenSessionStreaming"] > div').last().click()
  await expect(page).toHaveURL(`${built}?gallery&board=ScreenSessionStreaming`)
  const board = page.getByRole('region', { name: 'Session · streaming, thinking open' })
  await expect(board).toContainText('Where do fetches start in this repo, and what cancels them?')
  await expect(board.locator('[inert]')).toHaveCount(1)
  await page.keyboard.press('Escape')
  await expect(page).toHaveURL(`${built}?gallery`)
  await expect(heading(page, 'Screens — settings')).toBeAttached()
  // A board opened from far down its rows: Esc, or Back, brings its card back into view in its row.
  for (const [id, back] of [
    ['ScreenVoice', () => page.getByRole('button', { name: /^Back to the rows/ }).click()],
    ['ScreenSettingsXcode', () => page.keyboard.press('Escape')],
  ] as const) {
    const card = page.locator(`[data-board="${id}"]`)
    await card.scrollIntoViewIfNeeded()
    await card.locator('> div').last().click()
    await expect(page).toHaveURL(`${built}?gallery&board=${id}`)
    await back()
    await expect(page).toHaveURL(`${built}?gallery`)
    await expect(card).toBeInViewport()
  }
  expect(errors).toEqual([])
})

test('the prop switcher changes a board’s theme, and on a screen the apogee mode (design 1q)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?gallery&board=ScreenModels`)
  const board = page.getByRole('region', { name: 'Models · backends, one open' })
  await expect(board.locator('.ap[data-theme]').first()).toHaveAttribute('data-theme', 'dark')
  await page.getByRole('group', { name: 'Theme' }).getByRole('button', { name: 'Graphite Light' }).click()
  await expect(board.locator('.ap[data-theme]').first()).toHaveAttribute('data-theme', 'light')
  await expect(board).toContainText('Backends')
  await page.getByRole('group', { name: 'How apogee is found' }).getByRole('button', { name: 'missing' }).click()
  await expect(board).toContainText('Apogee isn’t installed')
  // A page takes the theme alone.
  await page.goto(`${built}?gallery&board=ColorTokens`)
  await expect(page.getByRole('group', { name: 'How apogee is found' })).toHaveCount(0)
  await page.getByRole('group', { name: 'Theme' }).getByRole('button', { name: 'Graphite Light' }).click()
  await expect(page.getByRole('region', { name: 'Foundations · Color tokens' }).locator('.ap[data-theme]').first()).toHaveAttribute('data-theme', 'light')
  expect(errors).toEqual([])
})

test('the App board opens live and takes the keys; a screen stands still and takes none (design 1q)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?gallery&board=App`)
  const app = page.getByRole('region', { name: 'Apogee · the whole app (interactive)' })
  await expect(app.locator('[data-gallery-live]')).toHaveCount(1)
  await expect(app.getByRole('navigation', { name: 'Places' })).toBeVisible()
  await app.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: /^Models/ }).click()
  await expect(app.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: /^Models/ })).toHaveAttribute('aria-current', 'page')
  // The live board takes the page's keys: ⌥⌘M opens its monitor, and Esc pressed inside it is the app's, not the gallery's.
  await page.keyboard.press('Alt+Meta+KeyM')
  await expect(app.getByRole('dialog', { name: 'System monitor' })).toBeVisible()
  await page.keyboard.press('Escape')
  await expect(page).toHaveURL(`${built}?gallery&board=App`)
  // The rows' frames take no key: ⌥⌘M opens no monitor in any of them.
  await page.goto(`${built}?gallery`)
  await expect(page.locator('[data-board="ScreenSessionStreaming"]')).toContainText('Where do fetches start')
  await page.keyboard.press('Alt+Meta+KeyM')
  await page.waitForTimeout(400)
  // By the DOM, not the accessibility tree, which leaves the rows' inert frames out.
  await expect(page.locator('[role="dialog"][aria-label="System monitor"]')).toHaveCount(0)
  expect(errors).toEqual([])
})

test('⌥⌘G opens the gallery from the app, outside its frame, and again leaves it for the app (design 1q)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=models&still=true`)
  await expect(page.getByRole('complementary', { name: 'Sidebar' })).toBeVisible()
  await page.keyboard.press('Alt+Meta+KeyG')
  await expect(heading(page, 'Gallery')).toBeVisible()
  await expect(page).toHaveURL(`${built}?start=models&still=true&gallery`)
  await expect(page.locator('#root > .ap > .ap-side')).toHaveCount(0)
  await page.keyboard.press('Alt+Meta+KeyG')
  await expect(heading(page, 'Gallery')).toHaveCount(0)
  await expect(page).toHaveURL(`${built}?start=models&still=true`)
  await expect(page.getByRole('complementary', { name: 'Sidebar' })).toBeVisible()
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=models&still=true`])
})

test('&theme= beside &board= opens the focused board in that theme, so each old picture has an address (design 1s)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?gallery&board=Logo&theme=light`)
  await expect(page.getByRole('group', { name: 'Theme' }).getByRole('button', { name: 'Graphite Light' })).toHaveAttribute('aria-pressed', 'true')
  await page.goto(`${built}?gallery&board=ComponentsTurn&theme=contrast`)
  await expect(page.getByRole('region', { name: 'Components · The turn' }).locator('[data-theme]').first()).toHaveAttribute('data-theme', 'contrast')
  await page.getByRole('group', { name: 'Theme' }).getByRole('button', { name: 'Graphite Dark' }).click()
  await expect(page).toHaveURL(`${built}?gallery=foundations&board=ComponentsTurn`)
  expect(errors).toEqual([])
})

test('the color page: 25 tokens in three themes, each ratio computed; the type page: spacing from 2 px (design 1q)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?gallery&board=ColorTokens`)
  const colors = page.getByRole('table', { name: 'Color tokens' })
  await expect(colors.getByRole('rowheader').filter({ hasText: /^[a-z]+\.[a-z-]+/ })).toHaveCount(25)
  await expect(colors.getByRole('row').filter({ hasText: 'status.warning' })).toContainText('Context at 80% and above, waiting for you')
  await expect(colors.getByRole('row').filter({ hasText: 'status.accent' })).toContainText('10.6:1')
  await expect(colors.getByRole('row').filter({ hasText: 'line.border' })).toContainText('—')
  await page.goto(`${built}?gallery&board=TypeSpace`)
  const type = page.getByRole('region', { name: 'Foundations · Type, space, shape' })
  await expect(type).toContainText('spacing from 2 px')
  await expect(type.locator('section').filter({ hasText: 'SPACING' })).toContainText('13 steps, from 2 to 32 px.')
  expect(errors).toEqual([])
})

test("each Components page draws the modules' own components over the CLI's recordings, in the board's theme, under its old board's heading (design 1q)", async ({ page }) => {
  const { errors, requests } = watch(page)
  /** A specimen's card on a page, by its name. */
  const card = (region: ReturnType<Page['getByRole']>, name: string) => region.locator('figure').filter({ has: page.locator('figcaption', { hasText: name }) }).first()
  await page.goto(`${built}?gallery&board=ComponentsModel`)
  const model = page.getByRole('region', { name: 'Components · Model, context, sources' })
  await expect(model.getByRole('heading', { level: 1 })).toHaveText('Model, context and sources')
  for (const module of ['SESSION', 'SESSIONS', 'STYLES']) await expect(model.getByRole('heading', { name: new RegExp(`^${module} · `) })).toBeVisible()
  await expect(card(model, 'Citation · a source opened')).toContainText('Because one flush pays for many commits')
  // Each kind's badge, as the view-model faces the recorded `models list`'s backends.
  for (const tag of ['local · this Mac', 'API · billed per use', 'subscription · claude-cli']) await expect(card(model, 'Model badge')).toContainText(tag)
  // The model menu: the recorded `models list`'s chat backends by kind.
  await expect(card(model, 'Model menu · ⇧⌘M or /model').getByRole('menu', { name: 'Switch model' })).toContainText('ON THIS MAC')
  await expect(card(model, 'Model menu · ⇧⌘M or /model')).toContainText('Models library…')
  // The inspector's context section, beside the session it replays.
  await expect(card(model, 'Inspector · context and what it holds').getByRole('complementary', { name: 'Inspector' })).toContainText('CONTEXT')
  // The sources the recorded answer drew on: three, the third pages 191–193.
  await expect(card(model, 'Sources · each excerpt')).toContainText('SOURCES · 3')
  await expect(card(model, 'Sources · each excerpt')).toContainText('pages 191–193')
  await page.goto(`${built}?gallery&board=ComponentsTurn`)
  const turn = page.getByRole('region', { name: 'Components · The turn' })
  await expect(turn.getByRole('heading', { level: 1 })).toHaveText('The turn')
  await expect(card(turn, 'Thinking section · live')).toContainText('thinking')
  await expect(card(turn, 'Thinking section · done, closed')).toContainText('thought')
  await expect(card(turn, 'Thinking section · streaming, tools run')).toContainText('They want partial-failure tolerance')
  await expect(card(turn, 'Recalled · a past chat named')).toContainText('asyncio timeouts in the scraper')
  await expect(card(turn, 'Notice line · a billed switch')).toContainText('switched to claude')
  await expect(card(turn, 'The answer · streaming')).toContainText('Timeouts that keep the batch going')
  await page.goto(`${built}?gallery&board=ComponentsPrompts`)
  const prompts = page.getByRole('region', { name: 'Components · Asking and failing' })
  await expect(prompts.getByRole('heading', { level: 1 })).toHaveText('Asking and failing')
  await expect(prompts).toContainText('Change a file?')
  await expect(prompts).toContainText('Should the retry budget apply per upload, per batch, or per host?')
  await expect(prompts).toContainText('vast didn’t load')
  await expect(prompts).not.toContainText('Not ported yet')
  await page.getByRole('group', { name: 'Theme' }).getByRole('button', { name: 'High Contrast' }).click()
  await expect(prompts.locator('[data-theme]')).toHaveCount(1)
  await expect(prompts.locator('[data-theme]')).toHaveAttribute('data-theme', 'contrast')
  await page.goto(`${built}?gallery&board=ComponentsMedia`)
  const media = page.getByRole('region', { name: 'Components · Making media' })
  await expect(media.getByRole('heading', { level: 1 })).toHaveText('Making media')
  // Each media piece is drawn by the place whose view-model builds its face over the recordings: none is typed into a `styles` specimen.
  for (const module of ['VIDEO', 'COMPOSE', 'VOICE']) await expect(media.getByRole('heading', { name: new RegExp(`^${module} · `) })).toBeVisible()
  await expect(media.getByRole('heading', { name: /^STYLES · / })).toHaveCount(0)
  await expect(media).toContainText('lighthouse-dusk.mp4')
  await expect(media).toContainText('hello-pytorch.wav')
  await expect(card(media, 'A run’s progress · stopped')).toContainText('121 frames · H.264')
  await expect(card(media, 'A run’s progress · stopped')).toContainText('cancelled')
  // The phase row's words are the view-model's, not a hand-typed `h264` (a clip record's own codec field says h264, the CLI's data).
  await expect(media).not.toContainText('frames · h264')
  await expect(card(media, 'Player · playing').getByRole('button', { name: 'Pause' })).toBeVisible()
  await expect(card(media, 'Install card · installing')).toContainText('Installing the video set')
  await expect(card(media, 'Install card · failed')).toContainText('The video set did not install')
  await expect(card(media, 'The install prompt')).toContainText('Install what video needs?')
  await expect(card(media, 'Play button · paused').getByRole('button')).toBeVisible()
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?gallery&board=ComponentsModel`, `${built}?gallery&board=ComponentsTurn`, `${built}?gallery&board=ComponentsPrompts`, `${built}?gallery&board=ComponentsMedia`])
})
