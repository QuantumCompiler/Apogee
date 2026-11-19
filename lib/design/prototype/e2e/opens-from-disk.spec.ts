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

test('a double-click opens lib/design/main.html from disk: chat, the Streaming session mid-reasoning, no console error, no network request (design 1v)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(built)
  await expect(page).toHaveTitle('Apogee')
  await expect(page.getByRole('complementary', { name: 'Sidebar' })).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Places' })).toBeVisible()
  // chat, the old data-props default: no monitor over it.
  await expect(monitor(page)).toHaveCount(0)
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Sessions ⌘1' })).toHaveAttribute('aria-current', 'page')
  const conversation = page.getByRole('region', { name: 'Conversation' })
  await expect(conversation).toContainText('Where do fetches start in this repo, and what cancels them?')
  await expect(conversation).toContainText('recalled 1 earlier chat')
  await expect(conversation).toContainText('[tool] fetch_url')
  await expect(conversation).toContainText('· 3 steps')
  await expect(page.getByRole('button', { name: 'Stop (Command period)' })).toBeVisible()
  // The web fonts are inlined: they load from the page itself. The faces that
  // loaded are the two the window uses; none loads when no face exists.
  expect(
    await page.evaluate(async () => (await document.fonts.ready, [...new Set([...document.fonts].filter((face) => face.status === 'loaded').map((face) => face.family))].sort())),
  ).toEqual(['IBM Plex Mono', 'IBM Plex Sans'])
  expect(await page.evaluate(() => [...document.fonts].filter((face) => face.status === 'error').length)).toBe(0)
  expect(errors).toEqual([])
  expect(requests).toEqual([built])
})

test('?start=monitor&still=true: the monitor over the Streaming session at chat\'s frame, launched behind it (design 1v)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=monitor&still=true`)
  await expect(monitor(page)).toBeVisible()
  await expect(page.getByRole('region', { name: 'Conversation' })).toContainText('Where do fetches start in this repo, and what cancels them?')
  await expect(page.getByRole('region', { name: 'Conversation' })).toContainText('· 3 steps')
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=monitor&still=true`])
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

test('each of the 11 places opens by click: Sessions its session window, an unported one its placeholder', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=monitor&still=true`)
  const places: Array<[string, string, string]> = [
    ['Models', '⌘2', '1h'], ['Suites', '⌘3', '1p'], ['Tasks', '⌘4', '1p'], ['Code graph', '⌘5', '1o'], ['Video', '⌘6', '1k'],
    ['Voice', '⌘7', '1m'], ['Knowledge', '⌘8', '1o'], ['Training', '⌘9', '1j'], ['Library', '⌘0', '1n'], ['Review', '⇧⌘R', '1n'],
  ]
  for (const [title, key, item] of [...places].reverse()) {
    const button = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: `${title} ${key}` })
    await button.click()
    await expect(button).toHaveAttribute('aria-current', 'page')
    await expect(page.getByRole('heading', { name: title, level: 1 })).toBeVisible()
    await expect(page.getByText(`${title} isn’t drawn here yet. Design item ${item} ports it.`)).toBeVisible()
  }
  // Sessions is ported (design 1u): the session window, no placeholder -- under the monitor, the Streaming session (1v).
  const sessions = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Sessions ⌘1' })
  await sessions.click()
  await expect(sessions).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'New session', level: 1 })).toBeVisible()
  await expect(page.getByRole('region', { name: 'Conversation' })).toContainText('Where do fetches start in this repo, and what cancels them?')
  await expect(page.getByText(/^Sessions isn’t drawn here yet/)).toHaveCount(0)
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
    // The toast, not the composer's status line (design 1u).
    await expect(page.getByRole('status').filter({ hasText: /^Theme: / })).toHaveText(`Theme: ${next}`)
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

/** The session's four plain presets (design 1u), each still, from disk, at its frame. */
const sessionFrames: ReadonlyArray<readonly [string, ReadonlyArray<string | RegExp>]> = [
  ['chat:new', ['What are we working on?', 'suite fast, the default', 'claude answers through the anthropic API, billed per use.', 'Private session instead']],
  ['chat:private', ['Private session', 'suite fast, the default', 'Nothing of this session is written: no transcript, no recall in later sessions.', 'private: nothing of this chat is written -- no transcript, no recall']],
  [
    'chat:play',
    [
      'execute · suite coding · orchestrating · in ingest',
      'project context: AGENTS.md, 12 words, in every request',
      'attaching . (2 files, 95 bytes)',
      'graph: 2 nodes, 1 edge (supported: python 1; skipped: .md 1)',
      'played summarize-verify',
      'stage 2/2 verify — asking chat (root): Here is a passage and a summary of it. Passage: The board…',
      'The board met on 14 October and moved the release to 3 November; Ana Ruiz owns the checklist.',
    ],
  ],
  ['chat:suite', ['Run this session under', 'Now: suite fast, the default', 'it does not fit', 'Run it anyway (--force): apogee prices it over this machine\'s memory']],
]

for (const [preset, words] of sessionFrames) {
  test(`?start=${preset}&still=true opens the session at its frame, the inspector's column reserved, with no console error and no request`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    for (const said of words) await expect(page.getByText(said).first()).toBeVisible()
    await expect(page.getByRole('complementary', { name: 'Inspector' })).toContainText('The inspector isn’t drawn here yet. Design item 1g ports it.')
    // Still: nothing moves.
    const before = await page.locator('main').innerText()
    await page.waitForTimeout(1500)
    expect(await page.locator('main').innerText()).toBe(before)
    expect(errors).toEqual([])
    expect(requests).toEqual([`${built}?start=${preset}&still=true`])
  })
}

/** The session's scenarios and states (design 1v), each still, from disk, at its frame: its words, and whether the inspector's column is kept. */
const scenarioFrames: ReadonlyArray<readonly [string, ReadonlyArray<string | RegExp>, boolean]> = [
  ['chat', ['Where do fetches start in this repo, and what cancels them?', 'recalled 1 earlier chat', 'asyncio timeouts in the scraper', '[tool] fetch_url', '· 3 steps'], true],
  ['chat:retry', ['Change a file?', 'root wants to write to an existing file', 'Show changes', 'Allow once', 'Allow for this session', 'Always allow', 'Deny', 'waiting for you'], true],
  ['chat:wal', ['switched to claude -- from the next turn', ' · billed per use from here', 'SOURCES · 3', 'Open at page 185', 'score 0.94 · lexical'], true],
  ['chat:crash', ['attaching crash-0912.png (1 file, 49 bytes)', 'vast didn’t load', /^vast: could not load the model at/, 'Unload and retry', 'Ask claude instead · billed'], true],
  ['chat:pr', ['Claude is asking', 'Should the retry budget apply per upload, per batch, or per host?', 'Per upload', 'Per batch', 'Per host', 'Skip', '1–3 pick · ⏎ answer'], true],
  ['chat:restarting', ['earlier in this conversation', 'a message could not be read and was skipped', /^switching to suite pair \(--suite pair\)/, 'Sent once the session is back: attach storage-engine-design.pdf', 'restarting'], false],
  ['chat:notsent', ['NOT SENT', 'apogee had stopped taking input, so this message never reached it.', 'Send again'], false],
  ['chat:ended', ['SESSION ENDED', 'apogee exited with status 2: fatal: the model went away', 'Start again', 'Sent when the session starts again: temperature 0.4 · compact'], false],
  ['chat:compacting', ['Where do fetches start in this repo, and what cancels them?', '· 3 steps'], true],
]

for (const [preset, words, inspector] of scenarioFrames) {
  test(`?start=${preset}&still=true opens the scenario at its frame, with no console error and no request (design 1v)`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    for (const said of words) await expect(page.getByText(said).first()).toBeVisible()
    await expect(page.getByRole('complementary', { name: 'Inspector' })).toHaveCount(inspector ? 1 : 0)
    const before = await page.locator('main').innerText()
    await page.waitForTimeout(1500)
    expect(await page.locator('main').innerText()).toBe(before)
    expect(errors).toEqual([])
    expect(requests).toEqual([`${built}?start=${preset}&still=true`])
  })
}

test('chat:pr: it opens with Per batch picked and Answer on, as the old frame does; another pick, and the answer written collapses the card to what was asked and said', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=chat:pr&still=true`)
  const answer = page.getByRole('button', { name: /^Answer/ })
  await expect(page.getByRole('radio', { name: /Per batch/ })).toBeChecked()
  await expect(answer).toBeEnabled()
  await page.getByRole('radio', { name: /Per host/ }).check()
  await expect(answer).toBeEnabled()
  await answer.click()
  await expect(page.getByText('asked: Should the retry budget apply per upload, per batch, or per host? → Per host')).toBeVisible()
  await expect(page.getByText('change answer')).toHaveCount(0)
  expect(errors).toEqual([])
})

test('chat:retry, live: Stop while the card stands closes it -- not answered, the turn ended first -- then stopped, and Send comes back', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=chat:retry`)
  await expect(page.getByText('Change a file?')).toBeVisible()
  await page.getByRole('button', { name: 'Stop (Command period)' }).click()
  await expect(page.getByText('write_file src/ingest/runner.py — not answered: the turn ended first')).toBeVisible()
  await expect(page.getByText('stopped', { exact: true })).toBeVisible()
  await expect(page.getByRole('button', { name: 'Stop (Command period)' })).toHaveCount(0)
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=chat:retry`])
})

test('chat:wal: citation 2 opens its source in place of 1, and Close closes it', async ({ page }) => {
  await page.goto(`${built}?start=chat:wal&still=true`)
  await page.getByRole('button', { name: /^Source 2: postgresql.conf/ }).first().click()
  await expect(page.getByRole('button', { name: 'Open the file' })).toBeVisible()
  await page.getByRole('button', { name: 'Close source' }).click()
  await expect(page.getByRole('button', { name: 'Close source' })).toHaveCount(0)
})

test("the inspector's placeholder takes its own 304 px column while open and none when closed: the main column is the window's less the sidebar and that column", async ({ page }) => {
  await page.goto(`${built}?start=chat:new&still=true`)
  const width = (selector: string) => page.locator(selector).evaluate((element) => element.getBoundingClientRect().width)
  const window = await width('#root > .ap')
  const sidebar = await width('.ap-side')
  const column = page.getByRole('region', { name: 'Conversation' }).or(page.getByRole('heading', { name: 'What are we working on?' }))
  await expect(column.first()).toBeVisible()
  const inspector = page.getByRole('complementary', { name: 'Inspector' })
  expect(await inspector.evaluate((element) => element.getBoundingClientRect().width)).toBe(304)
  const main = () => page.locator('main > div:nth-child(2) > div:first-child').evaluate((element) => element.getBoundingClientRect().width)
  expect(await main()).toBe(window - sidebar - 304)
  await page.getByRole('button', { name: 'Inspector (Option Command I)' }).click()
  await expect(inspector).toHaveCount(0)
  expect(await main()).toBe(window - sidebar)
  await page.getByRole('button', { name: /^Context window: / }).click()
  await expect(inspector).toBeVisible()
})

test('Choose a suite on chat:new opens the chooser over the recorded suites, the default marked now', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=chat:new&still=true`)
  await page.getByRole('button', { name: 'Choose a suite' }).click()
  await expect(page.getByText('Run this session under')).toBeVisible()
  await expect(page.getByRole('radio', { name: /^fast \(the default\)/ })).toContainText('now')
  await expect(page.getByText(/no recorded answer/)).toHaveCount(0)
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=chat:new&still=true`])
})

test('a chat preset in the missing and config-typo modes: no binary, and a launch the CLI refused, in its words', async ({ page }) => {
  await page.goto(`${built}?start=chat:new&still=true&apogee=missing`)
  await expect(page.getByRole('heading', { name: 'Apogee isn’t installed' })).toBeVisible()
  await page.goto(`${built}?start=chat:play&still=true&apogee=config-typo`)
  await expect(page.getByText("apogee refused to start this session: apogee execute: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member")).toBeVisible()
})

test('the composer: / lists the commands, @ the folder’s files, and a slash command it does not act on is said in the toast', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=chat:new&still=true`)
  const input = page.getByRole('textbox', { name: 'Message' })
  await input.fill('/mo')
  await expect(page.getByRole('listbox', { name: 'Commands' }).getByRole('option')).toHaveCount(2)
  await input.fill('look at @runner')
  await expect(page.getByRole('listbox', { name: 'Files' })).toContainText('src/ingest/runner.py')
  await input.press('Enter')
  await expect(page.getByRole('button', { name: 'Remove src/ingest/runner.py' })).toBeVisible()
  await input.fill('/recall off')
  await input.press('Escape')
  await input.press('Enter')
  await expect(page.getByRole('status').filter({ hasText: '/recall off' })).toHaveText('/recall off — noted; this prototype doesn’t act on it')
  expect(errors).toEqual([])
})
