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

test('each of the 11 places opens by click: Sessions its session window, Models its tabs (1h), Training its tabs (1j), Video its clips (1k), Voice its speeches (1m), Library and Review theirs (1n), the Code graph and Knowledge theirs (1o), Suites and Tasks theirs (1p); no placeholder is left', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=monitor&still=true`)
  const models = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Models ⌘2' })
  await models.click()
  await expect(models).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Models', level: 1 })).toBeVisible()
  await expect(page.getByRole('tab', { name: /^Backends/ })).toHaveAttribute('aria-selected', 'true')
  await expect(page.getByText(/^Models isn’t drawn here yet/)).toHaveCount(0)
  // Suites and Tasks are their places (1p): the recorded suites, the ledger's tasks, no placeholder.
  const suites = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Suites ⌘3' })
  await suites.click()
  await expect(suites).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Suites', level: 1 })).toBeVisible()
  await expect(page.getByRole('heading', { name: 'fast', level: 2 })).toBeVisible()
  await expect(page.getByText(/^Suites isn’t drawn here yet/)).toHaveCount(0)
  const tasks = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: /^Tasks ⌘4/ })
  await tasks.click()
  await expect(tasks).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Tasks', level: 1 })).toBeVisible()
  await expect(page.getByRole('heading', { name: 'Answer when asked', level: 2 })).toBeVisible()
  await expect(page.getByText(/^Tasks isn’t drawn here yet/)).toHaveCount(0)
  // Knowledge and the Code graph are their places (1o): Records, and the Functions view, no placeholder.
  const graph = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Code graph ⌘5' })
  await graph.click()
  await expect(graph).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Code graph', level: 1 })).toBeVisible()
  await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Functions' })).toHaveAttribute('aria-pressed', 'true')
  await expect(page.getByText(/^Code graph isn’t drawn here yet/)).toHaveCount(0)
  const knowledge = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Knowledge ⌘8' })
  await knowledge.click()
  await expect(knowledge).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Knowledge', level: 1 })).toBeVisible()
  await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Records' })).toHaveAttribute('aria-pressed', 'true')
  await expect(page.getByText(/^Knowledge isn’t drawn here yet/)).toHaveCount(0)
  // Library and Review are their places (1n): the agents with triage's page, the Reviews tab, no placeholder.
  const library = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Library ⌘0' })
  await library.click()
  await expect(library).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Library', level: 1 })).toBeVisible()
  await expect(page.getByRole('heading', { name: 'triage', level: 2 })).toBeVisible()
  await expect(page.getByText(/^Library isn’t drawn here yet/)).toHaveCount(0)
  const review = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Review ⇧⌘R' })
  await review.click()
  await expect(review).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Review', level: 1 })).toBeVisible()
  await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Reviews' })).toHaveAttribute('aria-pressed', 'true')
  await expect(page.getByText(/^Review isn’t drawn here yet/)).toHaveCount(0)
  // Training is its place (1j): its five tabs, Runs first, no placeholder.
  const training = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Training ⌘9' })
  await training.click()
  await expect(training).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Training', level: 1 })).toBeVisible()
  await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Runs' })).toHaveAttribute('aria-pressed', 'true')
  await expect(page.getByText(/^Training isn’t drawn here yet/)).toHaveCount(0)
  // Video is its place (1k): its clips, the newest record's page, no placeholder.
  const video = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Video ⌘6' })
  await video.click()
  await expect(video).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Video', level: 1 })).toBeVisible()
  await expect(page.getByRole('tab', { name: /^Clips/ })).toHaveAttribute('aria-selected', 'true')
  await expect(page.getByRole('heading', { name: 'lighthouse-dusk-2.mp4', level: 2 })).toBeVisible()
  await expect(page.getByText(/^Video isn’t drawn here yet/)).toHaveCount(0)
  // Voice is its place (1m): its speeches, the newest record's page, no placeholder.
  const voice = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Voice ⌘7' })
  await voice.click()
  await expect(voice).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('heading', { name: 'Voice', level: 1 })).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Speeches' }).getByRole('button', { name: /^hello-mlx\.wav/ })).toBeVisible()
  await expect(page.getByRole('heading', { name: 'hello-mlx.wav', level: 2 })).toBeVisible()
  await expect(page.getByText(/^Voice isn’t drawn here yet/)).toHaveCount(0)
  // Sessions is ported (design 1u): the session window, no placeholder -- under the monitor, the Streaming session (1v).
  const sessions = page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Sessions ⌘1' })
  await sessions.click()
  await expect(sessions).toHaveAttribute('aria-current', 'page')
  // Named from the session list (design 1g): the Streaming chat's listed name.
  await expect(page.getByRole('heading', { name: 'TaskGroup timeouts in the ingest runner', level: 1 })).toBeVisible()
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
  test(`?start=${preset}&still=true opens the session at its frame, the inspector beside it, with no console error and no request`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    for (const said of words) await expect(page.getByText(said).first()).toBeVisible()
    // The inspector (design 1g; 1u reserved its column with a placeholder).
    await expect(page.getByRole('complementary', { name: 'Inspector' })).toContainText('INSPECTOR')
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
  // The conversation's stopped line (the inspector's turn says stopped too, design 1g).
  await expect(page.getByRole('region', { name: 'Conversation' }).getByText('stopped', { exact: true })).toBeVisible()
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

test("the inspector takes its own 304 px column while open and none when closed: the main column is the window's less the sidebar and that column", async ({ page }) => {
  await page.goto(`${built}?start=chat:new&still=true`)
  const width = (selector: string) => page.locator(selector).evaluate((element) => element.getBoundingClientRect().width)
  const window = await width('#root > .ap')
  const sidebar = await width('.ap-side')
  const column = page.getByRole('region', { name: 'Conversation' }).or(page.getByRole('heading', { name: 'What are we working on?' }))
  await expect(column.first()).toBeVisible()
  const inspector = page.getByRole('complementary', { name: 'Inspector' })
  // The old pane's 304 px column and its 1 px left border (`flex: 0 0 304px`, content-box, `main.html:763` at b375fac9; design 1g).
  expect(await inspector.evaluate((element) => getComputedStyle(element).flexBasis)).toBe('304px')
  expect(await inspector.evaluate((element) => element.getBoundingClientRect().width)).toBe(305)
  const main = () => page.locator('main > div:nth-child(2) > div:first-child').evaluate((element) => element.getBoundingClientRect().width)
  expect(await main()).toBe(window - sidebar - 305)
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

/** Design 1g: the session list, the inspector, the execute door and the console. */
const sidebarSessions = (page: Page) => page.getByRole('navigation', { name: 'Sessions' })
const inspectorPane = (page: Page) => page.getByRole('complementary', { name: 'Inspector' })
const sections = ['SESSION', 'THIS TURN', 'CONTEXT', 'MODEL', 'HELPERS', 'RECALL'] as const

test('?start=execute&still=true: "New execute session" over the play session, with the prototype’s fields, note and command line (design 1g)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=execute&still=true`)
  const door = page.getByRole('dialog', { name: 'New execute session' })
  await expect(door).toBeVisible()
  await expect(door).toContainText('A session under a suite, its symphonies playable with /play -- or bare on one model.')
  await expect(door.getByRole('combobox', { name: 'Suite' })).toHaveValue('coding')
  await expect(door.getByRole('combobox', { name: 'Suite' }).locator('option').first()).toHaveText('The default suite (coding)')
  await expect(door.getByRole('combobox', { name: 'Model' }).locator('option:checked')).toHaveText('The suite’s chat member')
  await expect(door.getByRole('textbox', { name: 'Folder' })).toHaveValue('~/code/ingest')
  await expect(door.getByRole('checkbox', { name: /^Orchestrate: offer the suite’s symphonies/ })).toBeChecked()
  await expect(door).toContainText('Opened in a project -- a git repository, or a folder holding AGENTS.md or CLAUDE.md --')
  await expect(door).toContainText('cd ~/code/ingest && apogee execute --tools --suite coding --orchestrate')
  await expect(door.getByRole('button', { name: 'Cancel' })).toBeVisible()
  await expect(door.getByRole('button', { name: 'Open' })).toBeVisible()
  // Over the play session, its row current in the list.
  await expect(page.getByRole('region', { name: 'Conversation' })).toContainText('played summarize-verify')
  await expect(sidebarSessions(page).getByRole('button', { name: /^local The board’s minutes, summarized/ })).toHaveAttribute('aria-current', 'true')
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=execute&still=true`])
})

test('?start=console&still=true: the console with models list’s output, apogee reset refused, `models ` typed and ten words offered (design 1g)', async ({ page }) => {
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=console&still=true`)
  const console = page.getByRole('dialog', { name: 'Console' })
  await expect(console).toBeVisible()
  const log = console.getByRole('log', { name: 'Console output' })
  for (const line of ['apogee models list', 'Local backends', 'SafeTensors', '(not configured)  Llama3.2-1B-Instruct/safetensors/edaba290ce7f', '1 snapshot consumed by a conversion is folded -- --all lists it', 'Done.', 'apogee reset']) {
    await expect(log).toContainText(line)
  }
  await expect(log).toContainText('Not run here: deletes the data directory the running app stands on -- run it at a prompt, with the app closed.')
  await expect(console.getByRole('textbox', { name: 'Command' })).toHaveValue('models ')
  const offered = ['list', 'info', 'status', 'rename', 'pull', 'delete', 'quantize', 'convert', 'repair', 'migrate']
  for (const word of offered) await expect(console.getByRole('button', { name: word, exact: true })).toBeVisible()
  await expect(page.getByRole('button', { name: 'Console (Shift Command K)' })).toHaveAttribute('aria-expanded', 'true')
  // A word offered completes the line; Clear empties it; ⇧⌘K closes it.
  await console.getByRole('button', { name: 'info', exact: true }).click()
  await expect(console.getByRole('textbox', { name: 'Command' })).toHaveValue('models info ')
  await console.getByRole('button', { name: 'Clear' }).click()
  await expect(log).toContainText('Any apogee command runs here, as you’d type it at a prompt.')
  await page.keyboard.press('Shift+Meta+KeyK')
  await expect(console).toHaveCount(0)
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=console&still=true`])
})

test('every screen’s sidebar lists the recorded chats by date: Today and Yesterday open, Previous 7 days and Earlier closed (design 1g)', async ({ page }) => {
  for (const preset of ['chat:new', 'monitor', 'console', 'execute']) {
    await page.goto(`${built}?start=${preset}&still=true`)
    const list = sidebarSessions(page)
    await expect(list.getByRole('button', { name: /^SESSIONS/ }), preset).toContainText('13')
    for (const [group, count, open] of [['Today', '5', 'true'], ['Yesterday', '3', 'true'], ['Previous 7 days', '3', 'false'], ['Earlier', '2', 'false']] as const) {
      const heading = list.getByRole('button', { name: new RegExp(`^${group}`) })
      await expect(heading, `${preset} ${group}`).toContainText(count)
      await expect(heading).toHaveAttribute('aria-expanded', open)
    }
    await expect(list.getByRole('button', { name: /^local TaskGroup timeouts in the ingest runner/ })).toContainText('14:03')
  }
})

test('search filters the list -- every group open while it searches -- and a search with no match says so (design 1g)', async ({ page }) => {
  await page.goto(`${built}?start=chat:new&still=true`)
  const list = sidebarSessions(page)
  await list.getByRole('searchbox', { name: 'Search sessions' }).fill('timeouts')
  await expect(list.getByRole('button', { name: /^local TaskGroup timeouts in the ingest runner/ })).toBeVisible()
  await expect(list.getByRole('button', { name: /^local asyncio timeouts in the scraper/ })).toBeVisible()
  await expect(list.getByRole('button', { name: /^local Make fetch retries configurable/ })).toHaveCount(0)
  await expect(list).toContainText('2 matching · groups open while you search')
  await list.getByRole('searchbox', { name: 'Search sessions' }).fill('kubernetes')
  await expect(list).toContainText('No sessions match “kubernetes”.')
})

test("each row's menu opens Details, Rename, Compact and Delete, each in the shell's hosts (design 1g)", async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=chat:new&still=true`)
  const list = sidebarSessions(page)
  const open = async () => {
    await list.getByRole('button', { name: 'Actions for Where llama.cpp sizes the KV cache' }).click()
    const menu = page.getByRole('menu', { name: 'Where llama.cpp sizes the KV cache' })
    await expect(menu.getByRole('menuitem')).toHaveText(['Details…', 'Rename…', 'Compact…', 'Delete…'])
    return menu
  }
  await (await open()).getByRole('menuitem', { name: 'Details…' }).click()
  const details = page.getByRole('dialog', { name: 'Where llama.cpp sizes the KV cache' })
  await expect(details).toContainText('apogee chats info 20261119-111551-8425')
  await expect(details).toContainText('compacted')
  await details.getByRole('button', { name: 'Done' }).click()
  await (await open()).getByRole('menuitem', { name: 'Rename…' }).click()
  const rename = page.getByRole('dialog', { name: 'Rename session' })
  await expect(rename.getByRole('textbox', { name: 'Name' })).toHaveValue('Where llama.cpp sizes the KV cache')
  await rename.getByRole('textbox', { name: 'Name' }).fill('KV cache, sized')
  await rename.getByRole('button', { name: 'Rename' }).click()
  await expect(page.getByRole('status').filter({ hasText: 'Renamed to KV cache, sized' })).toBeVisible()
  await (await open()).getByRole('menuitem', { name: 'Compact…' }).click()
  await expect(page.getByRole('alertdialog', { name: 'Compact “Where llama.cpp sizes the KV cache”?' })).toContainText('apogee chats compact 20261119-111551-8425 --yes')
  await page.getByRole('alertdialog').getByRole('button', { name: 'Cancel' }).click()
  await (await open()).getByRole('menuitem', { name: 'Delete…' }).click()
  const confirm = page.getByRole('alertdialog', { name: 'Delete “Where llama.cpp sizes the KV cache”?' })
  await expect(confirm).toContainText("apogee chats delete 20261119-111551-8425 -- the conversation and its turns are removed, and this can't be undone.")
  await confirm.getByRole('button', { name: 'Delete' }).click()
  await expect(page.getByRole('status').filter({ hasText: 'Deleted Where llama.cpp sizes the KV cache' })).toBeVisible()
  expect(errors).toEqual([])
})

test('off Sessions no row is current, as the old list draws it: press Models and the open session’s row lets go; back on Sessions it is current again (design 1g)', async ({ page }) => {
  await page.goto(`${built}?start=chat&still=true`)
  const list = sidebarSessions(page)
  const streaming = list.getByRole('button', { name: /^local TaskGroup timeouts in the ingest runner/ })
  await expect(streaming).toHaveAttribute('aria-current', 'true')
  await page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Models ⌘2' }).click()
  await expect(list.locator('[aria-current="true"]')).toHaveCount(0)
  await page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Sessions ⌘1' }).click()
  await expect(streaming).toHaveAttribute('aria-current', 'true')
})

test('the console answers on every screen: at chat, a line nobody recorded prints the old "Done.", and a row’s Details… leaves the console open (design 1g)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=chat&still=true`)
  // The window drawn first, so React has its key listener (1e's lesson; under 1h's longer live runs the press could land before it).
  await expect(page.getByRole('heading', { level: 1 })).toBeVisible()
  await page.keyboard.press('Shift+Meta+KeyK')
  const console = page.getByRole('dialog', { name: 'Console' })
  await console.getByRole('textbox', { name: 'Command' }).fill('doctor')
  await console.getByRole('textbox', { name: 'Command' }).press('Enter')
  const log = console.getByRole('log', { name: 'Console output' })
  await expect(log).toContainText('apogee doctor')
  await expect(log).toContainText('Done.')
  await expect(log).not.toContainText('Didn’t start')
  await sidebarSessions(page).getByRole('button', { name: 'Actions for Where llama.cpp sizes the KV cache' }).click()
  await page.getByRole('menu', { name: 'Where llama.cpp sizes the KV cache' }).getByRole('menuitem', { name: 'Details…' }).click()
  await expect(page.getByRole('dialog', { name: 'Where llama.cpp sizes the KV cache' })).toBeVisible()
  await expect(console).toBeVisible()
  expect(errors).toEqual([])
})

const chatPresets = ['chat', 'chat:retry', 'chat:wal', 'chat:crash', 'chat:pr', 'chat:new', 'chat:private', 'chat:play', 'chat:suite', 'chat:restarting', 'chat:notsent', 'chat:ended', 'chat:compacting']

for (const preset of chatPresets) {
  test(`${preset}: ⌥⌘I opens the inspector with its six sections, and "Expand all" opens and closes them (design 1g)`, async ({ page }) => {
    const { errors } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    // chat:suite opens its chooser over the window, chat:compacting a row's menu: each closed first, as a person would.
    if (preset === 'chat:suite') await page.getByRole('button', { name: 'Cancel' }).click()
    if (preset === 'chat:compacting') await page.getByRole('button', { name: 'Close menu' }).click()
    await expect(page.getByRole('heading', { level: 1 })).toBeVisible()
    if ((await inspectorPane(page).count()) > 0) await page.keyboard.press('Alt+Meta+KeyI')
    await expect(inspectorPane(page)).toHaveCount(0)
    await page.keyboard.press('Alt+Meta+KeyI')
    const pane = inspectorPane(page)
    await expect(pane).toBeVisible()
    for (const section of sections) await expect(pane.getByRole('button', { name: new RegExp(`^${section}`) })).toBeVisible()
    await pane.getByRole('button', { name: 'Expand all' }).click()
    for (const section of sections) await expect(pane.getByRole('button', { name: new RegExp(`^${section}`) })).toHaveAttribute('aria-expanded', 'true')
    await pane.getByRole('button', { name: 'Collapse all' }).click()
    for (const section of sections) await expect(pane.getByRole('button', { name: new RegExp(`^${section}`) })).toHaveAttribute('aria-expanded', 'false')
    expect(errors).toEqual([])
  })
}

// MARK: - The Models place (design 1h)

const modelsTab = (page: Page, name: string) => page.getByRole('tab', { name: new RegExp(`^${name}`) })

const modelsFrames: ReadonlyArray<readonly [string, string, (page: Page) => Promise<void>]> = [
  ['models', 'Backends', async (page) => {
    const details = page.getByRole('complementary', { name: 'Model details' })
    await expect(details.getByRole('heading', { name: 'llama3.2-1b-Q4KM' })).toBeVisible()
    await expect(details).toContainText('weights 770 MiB + cache 544 MiB = 1.2 GiB')
    await expect(details).toContainText('LINEAGE')
    // A row's name begins with its glyph's word, as the session list's does (1g).
    await expect(page.getByRole('button', { name: /^local llama3\.2-1b-Q4KM / })).toHaveAttribute('aria-current', 'true')
  }],
  ['models:downloads', 'Jobs', async (page) => {
    await expect(page.getByText('running · step 2 of 3')).toBeVisible()
    await expect(page.getByText('GGUF F16, Q4_K_M, registered')).toBeVisible()
    await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Models ⌘2 · 1 running' })).toBeVisible()
  }],
  ['models:store', 'Stored weights', async (page) => {
    const set = page.getByRole('complementary', { name: 'Stored set' })
    await expect(set).toContainText('Llama3.2-1B-Instruct/safetensors/edaba290ce7f')
    await expect(set).toContainText('weights only · convert to run')
    await expect(set.getByRole('button', { name: 'Convert to GGUF…' })).toBeVisible()
  }],
  ['models:convert', 'Stored weights', async (page) => {
    const sheet = page.getByRole('dialog', { name: 'Convert Llama3.2-1B-Instruct' })
    await expect(sheet).toContainText('apogee models convert Llama3.2-1B-Instruct/safetensors/edaba290ce7f --register-with Q4_K_M')
    await expect(sheet.getByRole('button', { name: 'Q4_K_M' })).toHaveAttribute('aria-pressed', 'true')
  }],
  ['models:register', 'Stored weights', async (page) => {
    const sheet = page.getByRole('dialog', { name: 'Register Gemma4-26B-Instruct · mixed 4/8-bit (group 64)' })
    await expect(sheet.getByRole('textbox').first()).toHaveValue('Gemma4-26B-Instruct-mixed_4_8')
  }],
  ['models:roles', 'Roles', async (page) => {
    await expect(page.getByText('Roles resolve under suite fast (Suites changes it)')).toBeVisible()
    await expect(page.getByRole('combobox', { name: 'Model for Chat' })).toBeVisible()
  }],
  ['models:delete', 'Stored weights', async (page) => {
    const confirm = page.getByRole('alertdialog', { name: 'Delete Llama3.2-1B-Instruct/gguf/8d51e6c04a27?' })
    await expect(confirm).toContainText("backend 'llama3.2-1b-Q4KM' points into this")
    await expect(confirm.getByRole('button', { name: 'Delete it and the backends that run from it' })).toBeVisible()
  }],
  ['models:notran', 'Jobs', async (page) => {
    await expect(page.getByText('Stopped before it ran: nothing ran')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Run again' })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Dismiss' })).toBeVisible()
  }],
  ['download', 'Backends', async (page) => {
    const sheet = page.getByRole('dialog', { name: 'Download from Hugging Face' })
    await expect(sheet.getByRole('textbox', { name: 'Hugging Face repository' })).toHaveValue('meta-llama/Llama-3.2-1B-Instruct')
    await expect(sheet.getByRole('button', { name: 'Start download' })).toBeEnabled()
  }],
]

for (const [preset, tab, frame] of modelsFrames) {
  test(`${preset}: Models on its tab, sheet or confirm, still, with no console error and no request but the file (design 1h)`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    await expect(page.getByRole('heading', { name: 'Models', level: 1 })).toBeVisible()
    await expect(modelsTab(page, tab)).toHaveAttribute('aria-selected', 'true')
    await frame(page)
    expect(errors).toEqual([])
    expect(requests).toEqual([`${built}?start=${preset}&still=true`])
  })
}

test('the four tabs switch live; the store sheets, the adapters and pair sheets and the five confirms open from their buttons (design 1h)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=models&still=true`)
  const details = page.getByRole('complementary', { name: 'Model details' })
  // Backends: the adapters sheet, its Apply saying the CLI's line.
  await details.getByRole('button', { name: 'Edit adapters…' }).click()
  const adapters = page.getByRole('dialog', { name: 'Adapters on llama3.2-1b-Q4KM' })
  await adapters.getByRole('button', { name: 'Add a file…' }).click()
  await expect(adapters).toContainText('apogee config set-backend llama3.2-1b-Q4KM --adapters /Users/someone/Downloads/house-style.lora.gguf')
  await adapters.getByRole('button', { name: 'Apply' }).click()
  await expect(page.getByRole('status').filter({ hasText: "edited backend 'llama3.2-1b-Q4KM'" })).toBeVisible()
  // The two Backends confirms.
  await details.getByRole('button', { name: 'Delete with its weights…' }).click()
  await expect(page.getByRole('alertdialog', { name: 'Delete llama3.2-1b-Q4KM?' }).getByRole('button', { name: 'Delete its files and the backends that use them' })).toBeVisible()
  await page.getByRole('alertdialog').getByRole('button', { name: 'Cancel' }).click()
  await details.getByRole('button', { name: 'Remove the backend…' }).click()
  await expect(page.getByRole('alertdialog', { name: 'Remove the backend llama3.2-1b-Q4KM?' })).toContainText('apogee config delete-backend llama3.2-1b-Q4KM')
  await page.getByRole('alertdialog').getByRole('button', { name: 'Cancel' }).click()
  // The pair sheet on the vision MLX backend: the binary's offer, then Pair.
  await page.getByRole('button', { name: /^local Gemma4-E4B-4bit / }).click()
  await expect(details.getByText('there is no Python environment at /Users/someone/.apogee/training/venv -- the mlx backend runs mlx-lm there, never the system Python -- apogee train setup --with mlx')).toBeVisible()
  await details.getByRole('button', { name: 'Pair a drafter…' }).click()
  const pair = page.getByRole('dialog', { name: 'Pair a drafter with Gemma4-E4B-4bit' })
  await expect(pair.getByRole('radio', { name: 'Gemma4-E4B-Assistant/mlx/4d5e6f708192' })).toHaveAttribute('aria-checked', 'true')
  await pair.getByRole('button', { name: 'Pair' }).click()
  await expect(page.getByRole('status').filter({ hasText: 'Gemma4-E4B-4bit drafts with Gemma4-E4B-Assistant/mlx/4d5e6f708192' })).toBeVisible()
  // Stored weights: Register all, the F16's quantize and register sheets, the rename sheet, the set's and the model's deletes.
  await modelsTab(page, 'Stored weights').click()
  await page.getByRole('button', { name: 'Register the 2 not registered…' }).click()
  await expect(page.getByRole('alertdialog', { name: 'Register 2 stored models?' })).toContainText('apogee config scan --register')
  await page.getByRole('alertdialog').getByRole('button', { name: 'Cancel' }).click()
  await page.getByRole('button', { name: /^gguf\/3f0c2a9b7e41/ }).click()
  const set = page.getByRole('complementary', { name: 'Stored set' })
  await set.getByRole('button', { name: 'Quantize…' }).click()
  await expect(page.getByRole('dialog', { name: 'Quantize Llama3.2-1B-Instruct' })).toContainText('then  apogee config add-backend Llama3.2-1B-Instruct-Q4KM --type llamacpp --model-path <the new file>')
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel' }).click()
  await set.getByRole('button', { name: 'Register…' }).click()
  await expect(page.getByRole('dialog', { name: 'Register Llama3.2-1B-Instruct' }).getByRole('textbox').first()).toHaveValue('Llama3.2-1B-Instruct-F16')
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel' }).click()
  await set.getByRole('button', { name: 'Rename…' }).click()
  const rename = page.getByRole('dialog', { name: 'Rename Llama3.2-1B-Instruct' })
  await expect(rename.getByRole('button', { name: 'Rename' })).toBeDisabled()
  await rename.getByRole('textbox').fill('Llama-1B')
  await expect(rename).toContainText('apogee models rename Llama3.2-1B-Instruct Llama-1B')
  await rename.getByRole('button', { name: 'Cancel' }).click()
  await set.getByRole('button', { name: 'Delete all of it…' }).click()
  await expect(page.getByRole('alertdialog', { name: 'Delete all of Llama3.2-1B-Instruct?' })).toBeVisible()
  await page.getByRole('alertdialog').getByRole('button', { name: 'Cancel' }).click()
  await page.getByRole('button', { name: /^gguf\/8d51e6c04a27/ }).click()
  await set.getByRole('button', { name: 'Delete…' }).click()
  await page.getByRole('alertdialog', { name: 'Delete Llama3.2-1B-Instruct/gguf/8d51e6c04a27?' }).getByRole('button', { name: 'Delete', exact: true }).click()
  await expect(page.getByRole('status').filter({ hasText: 'Deleted Llama3.2-1B-Instruct/gguf/8d51e6c04a27' }).first()).toBeVisible()
  await expect(page.getByText("kept backend 'llama3.2-1b-Q4KM', which points at nothing now")).toBeVisible()
  // Jobs and Roles.
  await modelsTab(page, 'Jobs').click()
  await expect(page.getByText('Nothing has run here yet.')).toBeVisible()
  await modelsTab(page, 'Roles').click()
  await expect(page.getByRole('combobox', { name: 'Model for Utility' })).toBeVisible()
  expect(errors).toEqual([])
})

test('a conversion runs live and lands in Stored weights: the Llama snapshot to F16 and Q4_K_M, each with the backend its run registered (design 1h)', async ({ page }) => {
  test.setTimeout(60_000)
  const { errors } = watch(page)
  await page.goto(`${built}?start=models:convert`)
  await page.getByRole('dialog', { name: 'Convert Llama3.2-1B-Instruct' }).getByRole('button', { name: 'Convert' }).click()
  await expect(modelsTab(page, 'Jobs')).toHaveAttribute('aria-selected', 'true')
  await expect(page.getByText(/^done · /)).toBeVisible({ timeout: 30_000 })
  await expect(page.getByRole('status').filter({ hasText: 'Llama3.2-1B-Instruct: 2 sets made, 2 registered' }).first()).toBeVisible()
  await expect(page.getByText('Llama3.2-1B-Instruct-F16, Llama3.2-1B-Instruct-Q4KM')).toBeVisible()
  await page.getByRole('button', { name: 'Show the weights' }).click()
  await expect(modelsTab(page, 'Stored weights')).toHaveAttribute('aria-selected', 'true')
  await expect(page.getByRole('button', { name: /^gguf\/3f0c2a9b7e41/ })).toContainText('backend Llama3.2-1B-Instruct-F16')
  await expect(page.getByRole('button', { name: /^gguf\/8d51e6c04a27/ })).toContainText('backend Llama3.2-1B-Instruct-Q4KM')
  expect(errors).toEqual([])
})

test('a pull runs live and lands in Stored weights from its own documents: Start download, its bytes, its stages, its three sets (design 1h)', async ({ page }) => {
  test.setTimeout(90_000)
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=download`)
  await page.getByRole('dialog', { name: 'Download from Hugging Face' }).getByRole('button', { name: 'Start download' }).click()
  await expect(modelsTab(page, 'Jobs')).toHaveAttribute('aria-selected', 'true')
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Models ⌘2 · 1 running' })).toBeVisible()
  await expect(page.getByText(/^file \d of 9 · /)).toBeVisible({ timeout: 15_000 })
  await expect(page.getByText(/^done · /)).toBeVisible({ timeout: 40_000 })
  await expect(page.getByRole('status').filter({ hasText: 'Llama3.2-1B-Instruct · Q4_K_M is ready in the library' }).first()).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Models ⌘2', exact: true })).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Models ⌘2 · 1 running' })).toHaveCount(0)
  await modelsTab(page, 'Stored weights').click()
  await expect(page.getByRole('button', { name: /^gguf\/3f0c2a9b7e41/ })).toContainText('backend Llama3.2-1B-Instruct-F16')
  await expect(page.getByRole('button', { name: /^gguf\/8d51e6c04a27/ })).toContainText('backend Llama3.2-1B-Instruct-Q4KM')
  await expect(page.getByText('1 SafeTensors set a conversion has used is folded away, as models list folds it.')).toBeVisible()
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=download`])
})

test('the inspector’s Open in library lands on Backends at the session’s model, and Change helpers on Roles (design 1h)', async ({ page }) => {
  await page.goto(`${built}?start=chat&still=true`)
  const pane = inspectorPane(page)
  await pane.getByRole('button', { name: 'Expand all' }).click()
  await pane.getByRole('button', { name: 'Open in library' }).click()
  await expect(page.getByRole('heading', { name: 'Models', level: 1 })).toBeVisible()
  await expect(modelsTab(page, 'Backends')).toHaveAttribute('aria-selected', 'true')
  await expect(page.getByRole('complementary', { name: 'Model details' }).getByRole('heading', { name: 'root' })).toBeVisible()
  await page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Sessions ⌘1' }).click()
  await inspectorPane(page).getByRole('button', { name: 'Change helpers' }).click()
  await expect(modelsTab(page, 'Roles')).toHaveAttribute('aria-selected', 'true')
})

/** The Settings window (design 1i). */
const settingsWindow = (page: Page) => page.getByRole('dialog', { name: 'Settings' })
const settingsPane = (page: Page, label: string) => settingsWindow(page).getByRole('navigation', { name: 'Settings sections' }).getByRole('button', { name: label, exact: true })

/** The 13 Settings presets: the pane each opens, and words its frame shows, from the CLI's recordings and the old pane's copy. */
const settingsFrames: ReadonlyArray<readonly [string, string, ReadonlyArray<string | RegExp>]> = [
  ['settings:general', 'General', ['Model for new sessions', '/Users/someone/.local/bin/apogee · 0.2.0 · release channel', 'Not using /Users/someone/tools/apogee: `version` exited with status 1: error: broken install. This launch runs the one above.']],
  ['settings:appearance', 'Appearance', ['Graphite Dark', 'Answers 14 px · interface 13 px · code 12 px.', 'ELEMENT COLORS · GRAPHITE DARK', 'all text meets 4.5:1', '10.6:1']],
  ['settings:media', 'Video & voice', ['Kokoro-82M/mlx/6f708192a3b4, Kokoro-82M/voice/5f3a2c91d0e4', 'Caption new compositions', 'Video · ffmpeg h264']],
  ['settings:backends', 'Backends & keys', ['ON THIS MAC', 'CLOUD APIS · BILLED PER USE', 'Vendor-run tools consented: web_search, code_execution -- the vendor runs them under your key', 'PROVIDERS ON THIS MACHINE', '0 failing, 2 to look at']],
  ['settings:fixing', 'Backends & keys', ['nothing to fix', 'Fix what can be fixed (check --fix)']],
  ['settings:fixfailed', 'Backends & keys', ['it exited with status 1', '1 failing, 2 to look at', 'MCP · server: github: command not found on PATH: npx']],
  ['settings:permissions', 'Permissions', ['write_file', 'Read a web page. Ask means the first time for each website.', 'Nothing yet. Choosing “Always allow” on a permission card adds it here.']],
  ['settings:nosearch', 'Web access', ['docs.python.org', 'not set', 'Off until an address is set. web_search never pretends to succeed.']],
  ['settings:xcode', 'Xcode', ['xcode: Xcode 27.1 (27A9275), /Applications/Xcode.app/Contents/Developer', "Not in Apogee's config.", 'apogee mcp create xcode --from xcode', 'Add Apogee (dev) to Xcode 26.6 or later: Settings › Intelligence › Agents › Add an Agent']],
  ['settings:roles', 'Models & roles', ['The same roles as Models › Roles.', 'Roles resolve under suite fast (Suites changes it)', 'Open models library']],
  ['settings:web', 'Web access', ['developer.apple.com', 'http://searx.home.arpa:8080', 'On. Searches go to this address and nowhere else.']],
  ['settings:memory', 'Memory', ['Recall earlier sessions', '8 sessions summarized · in /Users/someone/.apogee/memory/chats.db', 'Forget all summaries…']],
  ['settings:keyboard', 'Keyboard', ['New session', '⌘1 – ⌘9, ⌘0', 'Hide the sidebar']],
]

for (const [preset, pane, words] of settingsFrames) {
  test(`${preset}: the Settings window on ${pane}, still, with no console error and no request but the file (design 1i)`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    await expect(settingsWindow(page)).toBeVisible()
    await expect(settingsPane(page, pane)).toHaveAttribute('aria-current', 'page')
    await expect(settingsWindow(page).getByRole('heading', { name: pane, level: 2 })).toBeVisible()
    for (const word of words) await expect(settingsWindow(page).getByText(word).first()).toBeVisible()
    if (preset === 'settings:media') await expect(settingsWindow(page).getByRole('combobox', { name: 'Video pipeline' })).toHaveValue('Wan2.2-TI2V-5B/mlx/5e6f708192a3')
    // The Streaming session behind it, at its starting frame.
    await expect(page.getByRole('region', { name: 'Conversation' })).toContainText('Where do fetches start in this repo, and what cancels them?')
    expect(errors).toEqual([])
    expect(requests).toEqual([`${built}?start=${preset}&still=true`])
  })
}

test('settings:fixfailed: the run’s failure in the toast, as the old frame shows it (design 1i)', async ({ page }) => {
  await page.goto(`${built}?start=settings:fixfailed&still=true`)
  await expect(page.getByRole('status').filter({ hasText: 'check --fix failed: it exited with status 1' })).toBeVisible()
})

test('⌘, opens Settings over any screen; all ten panes open from its sidebar; Done closes it (design 1i)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=models&still=true`)
  await expect(page.getByRole('heading', { name: 'Models', level: 1 })).toBeVisible()
  await expect(settingsWindow(page)).toHaveCount(0)
  await page.keyboard.press('Meta+Comma')
  await expect(settingsWindow(page)).toBeVisible()
  for (const pane of ['General', 'Appearance', 'Models & roles', 'Video & voice', 'Backends & keys', 'Permissions', 'Web access', 'Memory', 'Keyboard', 'Xcode']) {
    await settingsPane(page, pane).click()
    await expect(settingsPane(page, pane)).toHaveAttribute('aria-current', 'page')
    await expect(settingsWindow(page).getByRole('heading', { name: pane, level: 2 })).toBeVisible()
  }
  await expect(settingsWindow(page).getByText('Add Apogee (dev) to Xcode 26.6 or later: Settings › Intelligence › Agents › Add an Agent')).toBeVisible()
  await settingsWindow(page).getByRole('button', { name: 'Done' }).click()
  await expect(settingsWindow(page)).toHaveCount(0)
  // The sidebar's Settings button opens it too.
  await page.getByRole('button', { name: /^Settings/ }).click()
  await expect(settingsWindow(page)).toBeVisible()
  expect(errors).toEqual([])
})

test('Appearance restyles every screen at once, and its resets put it back: a color, the fonts and the text size (design 1i)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=settings:appearance&still=true`)
  const window = page.locator('#root > .ap')
  const variable = (name: string) => window.evaluate((element, wanted) => getComputedStyle(element).getPropertyValue(wanted).trim(), name)
  // A color, by its hex field: the window's variable, and what every screen paints with it.
  const hex = settingsWindow(page).getByRole('textbox', { name: 'Window hex value' })
  await hex.fill('#2a1f3d')
  await expect.poll(() => variable('--surface-window')).toBe('#2a1f3d')
  await expect(settingsWindow(page).getByText('1 change', { exact: true }).first()).toBeVisible()
  // The fonts and the text size.
  await settingsWindow(page).getByRole('combobox').filter({ hasText: 'IBM Plex Sans' }).selectOption('atkinson')
  await settingsWindow(page).getByRole('group', { name: 'Text size' }).getByRole('button', { name: 'Large', exact: true }).click()
  await expect.poll(() => variable('--t-body')).toBe('14px')
  expect(await variable('--font-ui')).toBe("'Atkinson Hyperlegible', system-ui, sans-serif")
  // Every screen: close the window and walk the places.
  await settingsWindow(page).getByRole('button', { name: 'Done' }).click()
  const places = page.getByRole('navigation', { name: 'Places' })
  for (const place of ['Models ⌘2', 'Suites ⌘3', 'Sessions ⌘1']) {
    await places.getByRole('button', { name: place }).click()
    await expect(page.locator('main')).toHaveCSS('font-size', '14px')
    await expect(window).toHaveCSS('background-color', 'rgb(42, 31, 61)')
  }
  // The resets: the row's, then Reset all, back to the theme's own.
  await page.keyboard.press('Meta+Comma')
  await settingsPane(page, 'Appearance').click()
  await settingsWindow(page).getByRole('button', { name: 'Reset Window to #131415' }).click()
  await expect.poll(() => variable('--surface-window')).toBe('#131415')
  await settingsWindow(page).getByRole('textbox', { name: 'Accent hex value' }).fill('#ff8800')
  await expect(settingsWindow(page).getByRole('button', { name: 'Reset all changes' })).toBeEnabled()
  await settingsWindow(page).getByRole('button', { name: 'Reset all changes' }).click()
  await expect.poll(() => variable('--status-accent')).toBe('#7FD4C1')
  await settingsWindow(page).getByRole('group', { name: 'Text size' }).getByRole('button', { name: 'Medium' }).click()
  await expect.poll(() => variable('--t-body')).toBe('13px')
  expect(errors).toEqual([])
})

test('Keyboard lists every key the shell binds, and Sessions’ keys, in the design’s words (design 1i)', async ({ page }) => {
  await page.goto(`${built}?start=settings:keyboard&still=true`)
  const keys = settingsWindow(page).getByRole('list', { name: 'Keys' }).getByRole('listitem')
  await expect(keys).toHaveCount(23)
  const listed = (await keys.allTextContents()).join('\n')
  for (const chord of ['⌘N', '⇧⌘N', '⌥⌘N', '⌥⌘I', '⇧⌘F', '⌥⌘M', '⇧⌘K', '⌘,', '⇧⌘R', '⇧⌘L', '⌃⌘S', '⌘1 – ⌘9, ⌘0']) expect(listed).toContain(chord)
})

test('Backends & keys: Add key… opens the field, a key saved goes and the field closes; Register… opens the sheet in the TUI’s words (design 1i)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=settings:backends&still=true`)
  const settings = settingsWindow(page)
  await settings.getByRole('button', { name: 'Add key…' }).first().click()
  const field = settings.getByLabel('Anthropic API key')
  await expect(field).toHaveAttribute('type', 'password')
  await expect(settings.getByRole('button', { name: 'Save key' })).toBeDisabled()
  await field.fill('sk-ant-typed-in-the-test')
  await settings.getByRole('button', { name: 'Save key' }).click()
  await expect(page.getByRole('status').filter({ hasText: 'stored a key for anthropic in /Users/someone/.apogee/config/credentials.json' })).toBeVisible()
  await expect(settings.getByLabel('Anthropic API key')).toHaveCount(0)
  await settings.getByRole('button', { name: 'Register…' }).first().click()
  const sheet = page.getByRole('dialog', { name: 'Found claude -- register it as a backend?' })
  await expect(sheet).toContainText('apogee providers register claude')
  await sheet.getByRole('button', { name: 'Register' }).click()
  await expect(page.getByRole('status').filter({ hasText: 'registered claude (claude-cli)' })).toBeVisible()
  expect(errors).toEqual([])
})

// ---- The Training place (design 1j) ----

const trainingTab = (page: Page, name: string) => page.getByRole('group', { name: 'Show' }).getByRole('button', { name, exact: true })
const toast = (page: Page, text: string | RegExp) => page.getByRole('status').filter({ hasText: text })

const trainingFrames: Array<[string, string, (page: Page) => Promise<void>]> = [
  ['training', 'Runs', async (page) => {
    await expect(page.getByText('[train] iter 2/4 · loss 0.6200 · lr 1.00e-04 · 10.0 it/s')).toBeVisible()
    await expect(page.getByRole('complementary', { name: 'Run' })).toContainText('failed the gate (46%) -- promote refuses it unless forced')
    await expect(page.getByText('v1 · run 20261026-065638 · adapter · scored on tiny-Q4_K_M.gguf (Q4_K_M) · active')).toBeVisible()
    // The sidebar's badge: the run going on, counted from the shared job list.
    await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Training ⌘9 · 1 running', exact: true })).toBeVisible()
  }],
  ['training:datasets', 'Datasets', async (page) => {
    await expect(page.getByRole('heading', { name: 'heat-notes', level: 2 })).toBeVisible()
    await expect(page.getByText('2 examples · chat · 449 bytes')).toBeVisible()
    await expect(page.getByText('32 eval items')).toBeVisible()
  }],
  ['training:pipelines', 'Pipelines', async (page) => {
    await expect(page.getByText('running here', { exact: true })).toBeVisible()
    await expect(page.getByText('training · iter 2/4 · loss 0.6200 · 10.0 it/s')).toBeVisible()
    await expect(page.getByText('Running skills')).toBeVisible()
  }],
  ['training:cycle', 'Cycle', async (page) => {
    await expect(page.getByText('pass · scored 100% · promoted v1 · 2 examples from directory')).toBeVisible()
    await expect(page.getByText('v1 (100%)')).toBeVisible()
  }],
  ['training:stalelock', 'Cycle', async (page) => {
    await expect(page.getByText(/^Stopped -- cycle\.lock is left by a process that is not running/)).toBeVisible()
    await expect(page.getByRole('button', { name: 'Run one pass…' })).toBeDisabled()
    await expect(page.getByRole('button', { name: 'Show in Finder' })).toBeVisible()
  }],
  ['training:unreadable', 'Cycle', async (page) => {
    await expect(page.getByText('The cycle history could not be read: /Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Halt…' })).toBeDisabled()
  }],
  ['training:cyclepass', 'Pipelines', async (page) => {
    await expect(page.getByText('A cycle pass: its candidate was discarded, and no other verb resumes it -- Run one pass on the Cycle tab runs the next pass.')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Resume…' })).toHaveCount(0)
  }],
  ['training:passrunning', 'Pipelines', async (page) => {
    await expect(page.getByText('A cycle pass, running now (process 67959): its candidate is the cycle’s, judged by its dual gate when the pass ends -- never from Runs.')).toBeVisible()
    await expect(page.getByText('cyclic · running · 0 of 1 stage passed')).toBeVisible()
  }],
  ['training:resumeheld', 'Pipelines', async (page) => {
    await expect(page.getByText('This pipeline is running now (process 67959); a resume is refused while it runs.')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Resume…' })).toBeDisabled()
  }],
  ['training:regimes', 'Regimes', async (page) => {
    await expect(page.getByRole('heading', { name: 'foundation', level: 2 })).toBeVisible()
    await expect(page.getByText('4. reasoning')).toBeVisible()
  }],
]

for (const [preset, tab, frame] of trainingFrames) {
  test(`${preset}: Training on its tab, still, its words visible, with no console error and no request but the file (design 1j)`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    await expect(page.getByRole('heading', { name: 'Training', level: 1 })).toBeVisible()
    await expect(trainingTab(page, tab)).toHaveAttribute('aria-pressed', 'true')
    await frame(page)
    expect(errors).toEqual([])
    expect(requests).toEqual([`${built}?start=${preset}&still=true`])
  })
}

test('training and training:datasets with no apogee: Runs and Datasets say it is not installed, as the old ones do (design 1j)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=training&still=true&apogee=missing`)
  await expect(page.getByText("Apogee isn't installed, so there is nothing to train.")).toBeVisible()
  await expect(page.getByText('Choose a run to see what it trained and how it did.')).toBeVisible()
  await page.goto(`${built}?start=training:datasets&still=true&apogee=missing`)
  await expect(page.getByText("Apogee isn't installed, so there is nothing to train.")).toBeVisible()
  await expect(page.getByText('Choose a dataset.')).toBeVisible()
  expect(errors).toEqual([])
})

test('the five tabs switch; a pipeline run plays to its abort, Resume… asks and the resume completes, live (design 1j)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=training:pipelines`)
  for (const tab of ['Runs', 'Cycle', 'Regimes', 'Datasets', 'Pipelines']) {
    await trainingTab(page, tab).click()
    await expect(trainingTab(page, tab)).toHaveAttribute('aria-pressed', 'true')
  }
  // The recorded run fails its second gate: said in the toast, its page offering the resume.
  await expect(toast(page, "pipeline aborted -- stage 'refusals' failed the cumulative eval gate (50% (1/2))")).toBeVisible({ timeout: 15_000 })
  await expect(page.getByText('Resume from stage 2, refusals')).toBeVisible()
  await page.getByRole('button', { name: 'Resume…' }).click()
  const confirm = page.getByRole('alertdialog', { name: 'Resume pipe-20261026-074112 from stage 2, refusals?' })
  await expect(confirm).toContainText('apogee train pipeline resume pipe-20261026-074112')
  await confirm.getByRole('button', { name: 'Resume' }).click()
  await expect(page.getByText('Resuming pipe-20261026-074112')).toBeVisible()
  await expect(toast(page, 'Pipeline pipe-20261026-074112: complete')).toBeVisible({ timeout: 15_000 })
  await expect(page.getByText('skills · complete · on tiny')).toBeVisible()
  expect(errors).toEqual([])
})

test('a cycle pass plays to its failure and halts the cycle; Resume… clears it, live (design 1j)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=training:cycle`)
  await page.getByRole('button', { name: 'Run one pass…' }).click()
  const confirm = page.getByRole('alertdialog', { name: 'Run the training cycle once?' })
  await expect(confirm).toContainText('apogee train cycle run')
  await confirm.getByRole('button', { name: 'Run one pass' }).click()
  await expect(page.getByText('THIS PASS, RUNNING HERE')).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Training ⌘9 · 1 running', exact: true })).toBeVisible()
  await expect(page.getByText(/^Halted: circuit breaker: 1 consecutive failed cycle\(s\) reached circuit_breaker_k 1\./)).toBeVisible({ timeout: 15_000 })
  await page.getByRole('button', { name: 'Resume…' }).click()
  await page.getByRole('alertdialog', { name: 'Resume the training cycle?' }).getByRole('button', { name: 'Resume' }).click()
  await expect(toast(page, 'cycle resumed (was halted: circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1); the failure count is reset')).toBeVisible()
  await expect(page.getByRole('button', { name: 'Halt…' })).toBeEnabled()
  expect(errors).toEqual([])
})

test('a promote: refused at its gate word for word, Change the promotion… reopens the sheet, forced into the adapter ledger refused in its mode, then a forced merge that plays to its line (design 1j)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=training`)
  await expect(page.getByRole('button', { name: 'New run…' })).toBeEnabled({ timeout: 10_000 })
  await page.getByRole('button', { name: 'Promote…' }).click()
  let sheet = page.getByRole('dialog', { name: 'Promote 20261021-051826' })
  await expect(sheet).toContainText('This run failed the gate (46%) -- promote refuses it unless forced.')
  await sheet.getByRole('textbox', { name: 'As backend' }).fill('based')
  await expect(sheet).toContainText('based holds adapter versions (active v1 of 1)')
  await sheet.getByRole('button', { name: 'Promote' }).click()
  const refused = page.getByRole('alertdialog', { name: "That didn't work" })
  await expect(refused).toContainText('apogee train: the eval gate failed for run 20261021-051826 (46%: 11/24 passed). Fix the training or the suite, then re-run eval, or pass --force to skip the gate')
  await refused.getByRole('button', { name: 'Change the promotion…' }).click()
  sheet = page.getByRole('dialog', { name: 'Promote 20261021-051826' })
  await expect(sheet.getByRole('alert')).toContainText('the eval gate failed for run 20261021-051826')
  await sheet.getByRole('checkbox', { name: /^Skip the gate/ }).check()
  await sheet.getByRole('button', { name: 'Promote' }).click()
  await expect(page.getByRole('alertdialog', { name: "That didn't work" })).toContainText("apogee train: 'based' holds adapter versions (active v1) applied over its base")
  await page.getByRole('alertdialog').getByRole('button', { name: 'Change the promotion…' }).click()
  sheet = page.getByRole('dialog', { name: 'Promote 20261021-051826' })
  await expect(sheet.getByRole('checkbox', { name: /^The adapter alone/ })).toBeChecked()
  await sheet.getByRole('checkbox', { name: /^The adapter alone/ }).uncheck()
  await sheet.getByRole('textbox', { name: 'As backend' }).fill('student-tuned')
  await expect(sheet).toContainText('apogee train promote 20261021-051826 --as student-tuned --force')
  await sheet.getByRole('button', { name: 'Promote' }).click()
  await expect(page.getByText('Promoting')).toBeVisible()
  await expect(toast(page, 'promoted to new backend student-tuned -> v1')).toBeVisible({ timeout: 15_000 })
  expect(errors).toEqual([])
})

test('a pull streams into the banner, and Prepare it runs datasets prepare, saying the CLI’s refusal of that folder (design 1j)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=training:datasets`)
  await page.getByRole('button', { name: 'Pull…' }).click()
  const sheet = page.getByRole('dialog', { name: 'Pull a dataset' })
  await expect(sheet.getByRole('button', { name: 'Pull' })).toBeDisabled()
  await sheet.getByRole('textbox', { name: 'Dataset reference' }).fill('someone/heat-notes')
  await expect(sheet).toContainText('apogee datasets pull someone/heat-notes')
  await sheet.getByRole('button', { name: 'Pull' }).click()
  await expect(page.getByText('Pulling')).toBeVisible()
  await expect(page.getByText('Pulled: 2 file(s), 136 KiB').first()).toBeVisible({ timeout: 15_000 })
  await page.getByRole('button', { name: 'Prepare it' }).click()
  await expect(toast(page, "apogee datasets: No supported data files found in '/Users/someone/.apogee/training/datasets/raw/someone--heat-notes'. Expected Parquet, JSON, JSONL or CSV files.")).toBeVisible({ timeout: 10_000 })
  await page.getByRole('button', { name: 'Dismiss' }).click()
  await expect(page.getByRole('button', { name: 'Prepare it' })).toHaveCount(0)
  expect(errors).toEqual([])
})

// The Video place and the runtime consent (design 1k): each preset at its frame, the click-through on rain, Stop on a run, and Settings' Install….
const videoPlace = (page: Page) => page.getByRole('navigation', { name: 'Places' })
const videoFrames: Array<[string, (page: Page) => Promise<void>]> = [
  ['video', async (page) => {
    await expect(page.getByRole('heading', { name: 'lighthouse-dusk.mp4', level: 2 })).toBeVisible()
    await expect(page.getByText('0:02.1 / 0:05.0')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Run again…' })).toBeVisible()
    await expect(page.getByText('run again from lighthouse-dusk-draft.mp4.json, seed held, 50 steps · Today 10:58')).toBeVisible()
    await expect(page.getByText('apogee video generate "A paper boat drifting down a rain-filled gutter, low angle, shallow depth of field" --draft -o ~/Movies/Apogee/paper-boat-drifting-draft.mp4')).toBeVisible()
  }],
  ['video:run', async (page) => {
    await expect(page.getByRole('heading', { name: 'lighthouse-dusk-3.mp4', level: 2 })).toBeVisible()
    await expect(page.getByText('step 38 of 50').first()).toBeVisible()
    await expect(page.getByRole('button', { name: 'Stop', exact: true })).toBeVisible()
    await expect(page.getByText('WHAT THE RUN SAYS')).toBeVisible()
    await expect(page.getByText('changed: 50 steps (--final)').first()).toBeVisible()
    await expect(videoPlace(page).getByRole('button', { name: 'Video ⌘6 · 1 running', exact: true })).toBeVisible()
  }],
  ['video:install', async (page) => {
    await expect(page.getByText('Installing the video set')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Stop', exact: true })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'rain-window.mp4', level: 2, exact: true })).toBeVisible()
    await expect(page.getByText('Recorded on linux-x86_64 (cuda · bfloat16). This is macos-arm64, so run here, the same seed may not make the same clip.')).toBeVisible()
  }],
  ['consent', async (page) => {
    const sheet = page.getByRole('dialog', { name: 'Install what video needs?' })
    await expect(sheet).toContainText('a LoRA loads through peft, which the video runtime does not have yet -- apogee train setup --with video')
    await expect(sheet).toContainText('apogee train setup --with video')
    await expect(sheet.getByRole('button', { name: 'Not now' })).toBeVisible()
    await expect(sheet.getByRole('button', { name: 'Install' })).toBeVisible()
  }],
]

for (const [preset, frame] of videoFrames) {
  test(`${preset}: the Video place at its frame, still, its words visible, with no console error and no request but the file (design 1k)`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    await expect(page.getByRole('heading', { name: 'Video', level: 1 })).toBeVisible()
    await expect(page.getByRole('tab', { name: /^Clips/ })).toHaveAttribute('aria-selected', 'true')
    await frame(page)
    expect(errors).toEqual([])
    expect(requests).toEqual([`${built}?start=${preset}&still=true`])
  })
}

test('the click-through on rain, live: Generate opens the consent, Install fails in the CLI’s words, Install again goes in, and the refused clip is made (design 1k)', async ({ page }) => {
  test.setTimeout(120_000)
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=video`)
  await page.getByRole('navigation', { name: 'Clips' }).getByRole('button', { name: /^rain-window\.mp4/ }).click()
  await page.getByRole('button', { name: 'Run again…' }).click()
  await expect(page.getByRole('heading', { name: 'From rain-window.mp4', level: 2 })).toBeVisible()
  await page.getByRole('button', { name: 'Generate', exact: true }).click()
  let sheet = page.getByRole('dialog', { name: 'Install what video needs?' })
  await expect(sheet).toContainText('a LoRA loads through peft, which the video runtime does not have yet -- apogee train setup --with video', { timeout: 10_000 })
  await sheet.getByRole('button', { name: 'Install' }).click()
  await expect(page.getByText('Installing the video set')).toBeVisible()
  await expect(page.getByText('The video set did not install')).toBeVisible({ timeout: 15_000 })
  await expect(toast(page, "Not installed: apogee train: pip install for the 'video' set failed (exit code 1):")).toBeVisible()
  await page.getByRole('button', { name: 'Dismiss' }).click()
  await page.getByRole('button', { name: 'Generate', exact: true }).click()
  sheet = page.getByRole('dialog', { name: 'Install what video needs?' })
  await sheet.getByRole('button', { name: 'Install' }).click({ timeout: 10_000 })
  await expect(page.getByText('Installed the video set')).toBeVisible({ timeout: 15_000 })
  await expect(toast(page, 'Installed: apogee train setup --with video -- making the clip again')).toBeVisible()
  await expect(page.getByRole('heading', { name: 'rain-window-2.mp4', level: 2 })).toBeVisible()
  await expect(toast(page, 'rain-window-2.mp4 is ready, with its record beside it')).toBeVisible({ timeout: 40_000 })
  await expect(page.getByRole('navigation', { name: 'Clips' }).getByRole('button', { name: /^rain-window-2\.mp4/ })).toBeVisible()
  await expect(page.getByText('diffusers 0.35.1 · peft 0.17.1 · torch 2.8.0')).toBeVisible()
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=video`])
})

test('Stop on a run, live, leaves nothing behind: no clip, no record; Dismiss puts the run away (design 1k)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=video:run`)
  await page.getByRole('button', { name: 'Stop', exact: true }).click()
  await expect(page.getByText('stopped by you')).toBeVisible()
  await expect(page.getByText('Nothing was left behind: no clip, no record, and the working folder is gone.')).toBeVisible()
  await expect(page.getByText('cancelled -- nothing was written (/Users/someone/Movies/Apogee/lighthouse-dusk-3.mp4 untouched)')).toBeVisible()
  await expect(videoPlace(page).getByRole('button', { name: 'Video ⌘6', exact: true })).toBeVisible()
  await page.getByRole('button', { name: 'Dismiss' }).click()
  await expect(page.getByRole('navigation', { name: 'Clips' }).getByRole('button', { name: /lighthouse-dusk-3\.mp4/ })).toHaveCount(0)
  expect(errors).toEqual([])
})

test('the player plays off the clock and pauses; still, it stands still (design 1k)', async ({ page }) => {
  await page.goto(`${built}?start=video`)
  const player = page.getByRole('button', { name: 'Play', exact: true }).first()
  await player.click()
  await expect(page.getByRole('button', { name: 'Pause' })).toBeVisible()
  await expect(page.getByText(/^0:0[3-5]\.\d \/ 0:05\.0$/)).toBeVisible({ timeout: 5_000 })
  await page.getByRole('button', { name: 'Pause' }).click()
  await page.goto(`${built}?start=video&still=true`)
  await page.getByRole('button', { name: 'Play', exact: true }).first().click()
  await expect(page.getByRole('button', { name: 'Pause' })).toBeVisible()
  await expect(page.getByText('0:02.1 / 0:05.0')).toBeVisible()
})

test("Settings' Install… on the video runtime's row opens the same consent sheet, in check's words (design 1k: the old Install peft…)", async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=settings:backends&still=true`)
  await page.getByRole('button', { name: 'Install…' }).click()
  const sheet = page.getByRole('dialog', { name: 'Install what video needs?' })
  await expect(sheet).toContainText('Video · runtime: not set up -- torch is not installed in the Python environment at /Users/someone/.apogee/training/venv')
  await sheet.getByRole('button', { name: 'Not now' }).click()
  await expect(sheet).toHaveCount(0)
  expect(errors).toEqual([])
})

// The compositions (design 1l): each Compose preset at its frame, the tabs, and live from the file a plan, a render scene by scene, Stop and Resume, and an edit previewed and rendered as a new video.
const compositions = (page: Page) => page.getByRole('navigation', { name: 'Compositions' })
const composeFrames: Array<[string, (page: Page) => Promise<void>]> = [
  ['compose', async (page) => {
    await expect(page.getByRole('heading', { name: 'How a heat pump works', level: 2 })).toBeVisible()
    await expect(page.getByText('0:09.6 / 0:33.5')).toBeVisible()
    await expect(page.getByText('A heat pump gathers it with a refrigerant that runs colder than the air itself.').first()).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Make changes', level: 2 })).toBeVisible()
    await expect(page.getByText('apogee video compose --from ~/Movies/Apogee/heat-pump.mp4 --dry-run -o ~/Movies/Apogee/heat-pump-edit.mp4')).toBeVisible()
    await expect(compositions(page).getByText('stopped at scene 5 · resumable')).toBeVisible()
    await expect(compositions(page).getByText('cut-short.mp4.json: not a JSON document')).toBeVisible()
    await expect(page.getByRole('tab', { name: /^Compositions/ })).toContainText('4')
  }],
  ['compose:edit', async (page) => {
    await expect(page.getByRole('heading', { name: 'Tides', level: 2 })).toBeVisible()
    await expect(page.getByText('the edit: 2 scenes, were 2')).toBeVisible()
    await expect(page.getByText('scene 2; its prompt changed')).toBeVisible()
    await expect(page.getByText('Render plans the edit again with planner: what renders can differ from this preview, and the planner is called a second time.')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Render as a new video' })).toBeVisible()
  }],
  ['compose:new', async (page) => {
    await expect(page.getByRole('heading', { name: 'New composition', level: 2 })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Tides', level: 3 })).toBeVisible()
    await expect(page.getByText('2 scenes · about 6 s of video')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Render the-tides-twice-a-day.mp4' })).toBeVisible()
    await expect(page.locator('label', { hasText: 'Voice model' })).toBeVisible()
    await expect(compositions(page).getByText('planned · nothing rendered')).toBeVisible()
  }],
  ['compose:tide', async (page) => {
    await expect(page.getByRole('heading', { name: 'Tide pools for kids', level: 2 })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Resume' })).toBeVisible()
    await expect(page.getByText('4 of 7 made')).toBeVisible()
    await expect(page.getByText('Stopped at scene 5’s clip. Every finished scene is kept, and resuming renders only what’s missing.', { exact: false })).toBeVisible()
    await expect(page.getByText('Resume it first: a stopped composition picks up where it stopped, and then it can be changed.')).toBeVisible()
  }],
  ['compose:differs', async (page) => {
    await expect(page.getByRole('heading', { name: 'Tides', level: 2 })).toBeVisible()
    await expect(page.getByText('The render planned again, and its changes differ from the preview')).toBeVisible()
    await expect(page.getByText('changed: narration')).toBeVisible()
    await expect(page.getByRole('tab', { name: /^Compositions/ })).toContainText('6')
  }],
]

for (const [preset, frame] of composeFrames) {
  test(`${preset}: the compositions at their frame, still, their words visible, with no console error and no request but the file (design 1l)`, async ({ page }) => {
    const { errors, requests } = watch(page)
    await page.goto(`${built}?start=${preset}&still=true`)
    await expect(page.getByRole('heading', { name: 'Video', level: 1 })).toBeVisible()
    await expect(page.getByRole('tab', { name: /^Compositions/ })).toHaveAttribute('aria-selected', 'true')
    await frame(page)
    expect(errors).toEqual([])
    expect(requests).toEqual([`${built}?start=${preset}&still=true`])
  })
}

test('Video’s Clips and Compositions tabs switch between the two modules, each counted from its own records (design 1l)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=video&still=true`)
  await expect(page.getByRole('tab', { name: /^Clips/ })).toContainText('5')
  await expect(page.getByRole('tab', { name: /^Compositions/ })).toContainText('4')
  await page.getByRole('tab', { name: /^Compositions/ }).click()
  await expect(compositions(page).getByRole('button', { name: /How a heat pump works/ })).toBeVisible()
  await expect(page.getByRole('button', { name: 'New composition' })).toBeEnabled()
  await page.getByRole('tab', { name: /^Clips/ }).click()
  await expect(page.getByRole('navigation', { name: 'Clips' })).toBeVisible()
  expect(errors).toEqual([])
})

test('live from the file: a new brief plans, its plan renders scene by scene, and the video is ready (design 1l)', async ({ page }) => {
  test.setTimeout(120_000)
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=compose`)
  await page.getByRole('button', { name: 'New composition' }).click()
  await page.getByRole('button', { name: 'Plan', exact: true }).click()
  await expect(page.getByText('planning the scenes with fastroot · one structured call, thinking off')).toBeVisible()
  await expect(page.getByRole('heading', { name: 'Tides', level: 3 })).toBeVisible({ timeout: 15_000 })
  await expect(toast(page, 'Planned the-tides-twice-a-day.mp4: nothing is rendered yet')).toBeVisible()
  await page.getByRole('button', { name: 'Render the-tides-twice-a-day.mp4' }).click()
  await expect(page.getByText(/^speaking scene [12]$/)).toBeVisible({ timeout: 10_000 })
  await expect(videoPlace(page).getByRole('button', { name: 'Video ⌘6 · 1 running', exact: true })).toBeVisible()
  await expect(page.getByText(/^rendering scene 1 · step \d+ of 50$/)).toBeVisible({ timeout: 15_000 })
  await expect(toast(page, 'the-tides-twice-a-day.mp4 is ready, its captions beside it as an .srt')).toBeVisible({ timeout: 30_000 })
  await expect(page.getByText('0:00.0 / 0:05.5')).toBeVisible()
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=compose`])
})

test('live from the file: the tide resumes, stops with its finished scenes kept, and resumes again to the end (design 1l)', async ({ page }) => {
  test.setTimeout(120_000)
  const { errors } = watch(page)
  await page.goto(`${built}?start=compose:tide`)
  await page.getByRole('button', { name: 'Resume' }).click()
  await expect(page.getByText(/^rendering scene 6 · step \d+ of 50$/)).toBeVisible({ timeout: 15_000 })
  await page.getByRole('button', { name: 'Stop', exact: true }).click()
  await expect(page.getByText(/^Stopped at scene 6’s clip, step \d+ of 50\./)).toBeVisible()
  await expect(page.getByText('5 of 7 made')).toBeVisible()
  await page.getByRole('button', { name: 'Resume' }).click()
  await expect(page.getByText(/^rendering scene 6 · step \d+ of 50$/)).toBeVisible({ timeout: 10_000 })
  await expect(page.getByText('5 of 7 made · scene 6', { exact: false })).toBeVisible()
  await expect(toast(page, 'tide-pools.mp4 is ready, its captions beside it as an .srt')).toBeVisible({ timeout: 30_000 })
  expect(errors).toEqual([])
})

test('live from the file: an edit previews on paper and renders as a new video, the page saying its render came out otherwise (design 1l)', async ({ page }) => {
  test.setTimeout(120_000)
  const { errors } = watch(page)
  await page.goto(`${built}?start=compose:edit`)
  await expect(page.getByText('scene 2; its prompt changed')).toBeVisible()
  await page.getByRole('button', { name: 'Render as a new video' }).click()
  await expect(compositions(page).getByRole('button', { name: /tides-edit\.mp4/ })).toBeVisible({ timeout: 10_000 })
  await expect(toast(page, 'tides-edit.mp4 is ready, its captions beside it as an .srt')).toBeVisible({ timeout: 30_000 })
  await expect(page.getByText('The render planned again, and its changes differ from the preview')).toBeVisible()
  expect(errors).toEqual([])
})

const speechesList = (page: Page) => page.getByRole('navigation', { name: 'Speeches' })
const sample = 'Apogee runs every model on this Mac unless you pick a cloud one. When it does use the cloud, it tells you, every time.'

for (const apogee of ['installed', 'missing', 'config-typo'] as const) {
  test(`voice at ${apogee}: the Voice place at its frame, still -- the header, the two recorded speeches, the speak card and the selected speech -- with no console error and no request but the file (design 1m)`, async ({ page }) => {
    const { errors, requests } = watch(page)
    const url = `${built}?start=voice&still=true&apogee=${apogee}`
    await page.goto(url)
    await expect(page.getByRole('heading', { name: 'Voice', level: 1 })).toBeVisible()
    await expect(page.getByText('Text to speech, on this Mac')).toBeVisible()
    await expect(page.getByRole('button', { name: 'New speech' })).toBeVisible()
    await expect(speechesList(page).getByRole('button', { name: /^hello-mlx\.wav/ })).toBeVisible()
    await expect(speechesList(page).getByRole('button', { name: /^hello-pytorch\.wav/ })).toHaveAttribute('aria-current', 'true')
    const speak = page.getByRole('region', { name: 'Speak' })
    await expect(speak.getByRole('textbox').first()).toHaveValue(sample)
    await expect(speak).toContainText('118 / 4,000')
    await expect(speak).toContainText('2 sentences · split where . ! or ? meets a space, never by a model')
    await expect(speak.getByRole('slider', { name: 'Speed' })).toBeVisible()
    await expect(speak).toContainText('1.0×')
    await expect(speak).toContainText('Save as')
    await expect(speak.getByText(`apogee voice say "${sample}" -o ~/Music/Apogee/apogee-runs-every.wav`)).toBeVisible()
    const speech = page.getByRole('region', { name: 'Speech' })
    await expect(speech.getByRole('heading', { name: 'hello-pytorch.wav', level: 2 })).toBeVisible()
    await expect(speech).toContainText('af_heart · 1.0× · 0.1 s · Oct 22')
    for (const action of ['Use these settings', 'Use this voice for compositions', 'Show in Finder']) await expect(speech.getByRole('button', { name: action })).toBeVisible()
    await expect(speech.getByText('0:00.1 / 0:00.1')).toBeVisible()
    await expect(speech.getByRole('button', { name: 'Play', exact: true })).toBeVisible()
    await expect(speech).toContainText('16-bit mono WAV · 24 kHz · 0.1 s')
    await expect(speech).toContainText('STARTS')
    await expect(speech).toContainText('LASTS')
    await expect(speech.getByRole('button', { name: 'Play from sentence 2' })).toContainText('How are you?0.05 s0.05 s')
    await expect(speech).toContainText('RECORD · hello-pytorch.wav.json')
    await expect(speech).toContainText('PyTorch · kokoro 0.0-stub · torch 0.0-stub')
    if (apogee === 'installed') {
      // The store's one voice model is the record's: the CLI speaks with it unnamed.
      await expect(speech.getByText('apogee voice say "Hello there. How are you?" -o ~/Music/Apogee/hello-pytorch.wav')).toBeVisible()
      await expect(page.getByRole('button', { name: 'Kokoro-82M · PyTorch' })).toBeVisible()
      await expect(speak.getByRole('button', { name: /^af_heart/ })).toHaveAttribute('aria-pressed', 'true')
      await expect(speak).toContainText('AMERICAN ENGLISH · 2')
      await expect(speak).toContainText('BRITISH ENGLISH · 1')
      await expect(speak.getByRole('button', { name: 'Speak', exact: true })).toBeEnabled()
      await expect(speak).toContainText('Speaks on PyTorch, through Kokoro: the voice model as Kokoro publishes it. Nothing leaves this Mac.')
    } else {
      // With the store unread, the record's model is named.
      await expect(speech.getByText('apogee voice say "Hello there. How are you?" -m Kokoro-82M/voice/5f3a2c91d0e4 -o ~/Music/Apogee/hello-pytorch.wav')).toBeVisible()
      if (apogee === 'missing') {
        await expect(speak.getByRole('button', { name: 'Speak', exact: true })).toBeDisabled()
        await expect(speak).toContainText('apogee is not installed')
      } else {
        // The binary is found: Speak runs, and the CLI refuses, as its store read was refused.
        await expect(speak.getByRole('button', { name: 'Speak', exact: true })).toBeEnabled()
        await expect(page.getByRole('button', { name: 'the store could not be read' })).toBeVisible()
        await expect(speak).toContainText('apogee models: /Users/someone/.apogee/config/config.json: not valid JSON')
      }
    }
    expect(errors).toEqual([])
    expect(requests).toEqual([url])
  })
}

test('live from the file: Speak runs the hand-made stream end to end -- ⌘7 says 1 running while it speaks -- and the new speech ends up selected (design 1m)', async ({ page }) => {
  test.setTimeout(60_000)
  const { errors, requests } = watch(page)
  await page.goto(`${built}?start=voice`)
  await page.getByRole('button', { name: 'Speak', exact: true }).click()
  // The running row: its pulse says "speaking" before its file.
  await expect(speechesList(page).getByRole('button', { name: /^speaking apogee-runs-every\.wav/ })).toContainText('speaking · af_heart')
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Voice ⌘7 · 1 running', exact: true })).toBeVisible()
  await expect(page.getByText(/^speaking 2 sentences as af_heart/)).toBeVisible()
  await expect(toast(page, 'apogee-runs-every.wav is ready · 0.1 s, measured from its header')).toBeVisible({ timeout: 15_000 })
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Voice ⌘7', exact: true })).toBeVisible()
  await expect(speechesList(page).getByRole('button', { name: /^apogee-runs-every\.wav/ })).toHaveAttribute('aria-current', 'true')
  const speech = page.getByRole('region', { name: 'Speech' })
  await expect(speech.getByRole('heading', { name: 'apogee-runs-every.wav', level: 2 })).toBeVisible()
  await expect(speech).toContainText('af_heart · 1.0× · 0.1 s · Today 12:00')
  await expect(speech.getByRole('button', { name: 'Play from sentence 1' })).toContainText('Apogee runs every model on this Mac unless you pick a cloud one.')
  await expect(page.getByRole('region', { name: 'Speak' }).getByRole('textbox').nth(1)).toHaveValue('apogee-runs-every-2.wav')
  expect(errors).toEqual([])
  expect(requests).toEqual([`${built}?start=voice`])
})

test('the speak card refuses text over 4,000 characters and a name that exists, in the old words, Speak off (design 1m)', async ({ page }) => {
  await page.goto(`${built}?start=voice&still=true`)
  const speak = page.getByRole('region', { name: 'Speak' })
  await speak.getByRole('textbox').nth(1).fill('hello-pytorch.wav')
  await expect(speak.getByRole('alert')).toHaveText('~/Music/Apogee/hello-pytorch.wav exists. A speech never writes over a file; name another.')
  await expect(speak.getByRole('button', { name: 'Speak', exact: true })).toBeDisabled()
  await speak.getByRole('textbox').nth(1).fill('fresh.wav')
  await speak.getByRole('textbox').first().fill('a'.repeat(4001))
  await expect(speak.getByRole('alert')).toHaveText('The text is 4,000 characters at most; this is 4,001.')
  await expect(speak).toContainText('4,001 / 4,000')
  await expect(speak.getByRole('button', { name: 'Speak', exact: true })).toBeDisabled()
})

test('a sentence plays from its start, lit, and the player pauses; still, it stands still; Use this voice for compositions says so (design 1m)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=voice&still=true`)
  const speech = page.getByRole('region', { name: 'Speech' })
  const transparent = 'rgba(0, 0, 0, 0)'
  const lit = rgb(tokens.colors.themes['dark']?.colors['surface.selection'] ?? '')
  await speech.getByRole('button', { name: 'Play from sentence 2' }).click()
  await expect(speech.getByRole('button', { name: 'Pause' })).toBeVisible()
  await expect(speech.getByText('0:00.1 / 0:00.1')).toBeVisible()
  await expect(speech.getByRole('button', { name: 'Play from sentence 2' })).toHaveCSS('background-color', lit)
  await expect(speech.getByRole('button', { name: 'Play from sentence 1' })).toHaveCSS('background-color', transparent)
  await speech.getByRole('button', { name: 'Pause' }).click()
  await expect(speech.getByRole('button', { name: 'Play', exact: true })).toBeVisible()
  await expect(speech.getByRole('button', { name: 'Play from sentence 2' })).toHaveCSS('background-color', transparent)
  await speech.getByRole('button', { name: 'Use this voice for compositions' }).click()
  await expect(toast(page, 'af_heart is now the voice for new speech and compositions')).toBeVisible()
  await page.getByRole('button', { name: 'New speech' }).click()
  await expect(speech).toHaveCount(0)
  await expect(page.getByRole('region', { name: 'Speak' }).getByRole('textbox').first()).toHaveValue('')
  expect(errors).toEqual([])
})

const definitions = (page: Page) => page.getByRole('navigation', { name: 'Definitions' })
const editorSheet = (page: Page) => page.getByRole('dialog')
const confirmHost = (page: Page) => page.getByRole('alertdialog')
const typoWords = "/Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)"

/** What each Library and Review preset shows at its frame, still, in each mode the old prototype draws it -- its regions, tabs, controls, states and words (design 1n). */
const libraryFrames: Record<string, (page: Page, apogee: 'installed' | 'missing' | 'config-typo') => Promise<void>> = {
  library: async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Agents' })).toHaveAttribute('aria-pressed', 'true')
    await expect(page.getByRole('button', { name: 'New agent…' })).toBeVisible()
    if (apogee === 'installed') {
      await expect(definitions(page).getByRole('button', { name: /^triage/ })).toHaveAttribute('aria-current', 'true')
      await expect(definitions(page).getByRole('button')).toHaveCount(8)
      await expect(page.getByRole('heading', { name: 'triage', level: 2 })).toBeVisible()
      await expect(page.getByRole('button', { name: 'Delete…' })).toBeVisible()
      await expect(page.getByText('Sort incoming issues by area', { exact: true })).toBeVisible()
      await expect(page.getByText('Model: the default · tools: read-only')).toBeVisible()
      await expect(page.getByRole('button', { name: 'Edit prompts/triage.txt' })).toBeVisible()
      await expect(page.getByRole('button', { name: 'Edit schemas/triage-output.json' })).toBeVisible()
      await expect(page.getByText('apogee analyze --agent triage runs it.')).toBeVisible()
    } else {
      await expect(definitions(page)).toContainText(apogee === 'missing' ? "Apogee isn't installed, so there are no agents to list." : `The agents could not be read: apogee agents: ${typoWords}`)
      await expect(page.getByText('Choose an agent.')).toBeVisible()
    }
  },
  'library:symphonies': async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Symphonies' })).toHaveAttribute('aria-pressed', 'true')
    if (apogee === 'installed') {
      await expect(definitions(page).getByRole('button')).toHaveCount(9)
      await expect(page.getByRole('heading', { name: 'critique-revise', level: 2 })).toBeVisible()
      await expect(page.getByText('1. critique · asks utility')).toBeVisible()
      await expect(page.getByText('A shipped starter: apogee symphonies edit critique-revise makes it yours, at a prompt.')).toBeVisible()
      await expect(page.getByRole('textbox', { name: 'Its input' })).toHaveValue('A draft README for the tool.')
      await expect(page.getByText('critique · utility (writer)')).toBeVisible()
      await expect(page.getByText('revise · chat (writer)')).toBeVisible()
      await expect(page.getByText('OUTPUT')).toBeVisible()
    } else {
      await expect(definitions(page)).toContainText(apogee === 'missing' ? "Apogee isn't installed, so there are no symphonies to list." : `The symphonies could not be read: apogee symphonies: ${typoWords}`)
      await expect(page.getByText('Choose a symphony.')).toBeVisible()
    }
  },
  'library:mcp': async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'MCP servers' })).toHaveAttribute('aria-pressed', 'true')
    await expect(page.getByText('PRESETS APOGEE SHIPS')).toBeVisible()
    if (apogee === 'installed') {
      await expect(page.getByRole('heading', { name: 'code', level: 2 })).toBeVisible()
      await expect(page.getByRole('button', { name: 'Disable' })).toBeVisible()
      await expect(page.getByRole('button', { name: 'Remove…' })).toBeVisible()
      await expect(page.getByText('connected · 2 tools: run_python, write_input · environment: none')).toBeVisible()
      await expect(page.getByText('code-exec', { exact: true })).toBeVisible()
      await expect(page.getByText("'self' is Apogee's own tools, served for another MCP host", { exact: false })).toBeVisible()
    } else if (apogee === 'missing') {
      await expect(page.getByText("Apogee isn't installed, so there are no presets to list.")).toBeVisible()
    } else {
      await expect(definitions(page)).toContainText(`The MCP servers could not be read: apogee mcp: ${typoWords}`)
    }
  },
  'library:unsaved': async (page, apogee) => {
    if (apogee !== 'installed') return
    await expect(editorSheet(page).getByRole('heading', { name: 'triage · prompts/triage.txt' })).toBeVisible()
    await expect(editorSheet(page).getByText('/Users/someone/.apogee/prompts/triage.txt')).toBeVisible()
    await expect(editorSheet(page).getByRole('textbox', { name: 'The file' })).toHaveValue(/File each issue under one area\.\n$/)
    await expect(confirmHost(page).getByRole('heading', { name: 'Discard your changes to triage.txt?' })).toBeVisible()
    await expect(confirmHost(page)).toContainText("What you typed isn't saved, and closing loses it.")
    await expect(confirmHost(page).getByRole('button', { name: 'Keep editing' })).toBeVisible()
    await expect(confirmHost(page).getByRole('button', { name: 'Discard' })).toBeVisible()
  },
  'library:refused': async (page, apogee) => {
    if (apogee !== 'installed') return
    await expect(editorSheet(page).getByRole('heading', { name: 'notes-digest' })).toBeVisible()
    await expect(editorSheet(page).getByText("apogee symphonies: /Users/someone/.apogee/symphonies/notes-digest.yaml: stages[1]: unknown key 'temprature'", { exact: false })).toBeVisible()
    await expect(editorSheet(page).getByRole('button', { name: 'Put it back as it was' })).toBeVisible()
    await expect(confirmHost(page).getByRole('heading', { name: 'notes-digest.yaml holds the text apogee refused' })).toBeVisible()
    for (const name of ['Keep editing', 'Leave it', 'Put it back as it was']) await expect(confirmHost(page).getByRole('button', { name })).toBeVisible()
  },
  'library:image': async (page, apogee) => {
    if (apogee !== 'installed') return
    await expect(page.getByRole('heading', { name: 'describe-answer', level: 2 })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Choose an image…' })).toBeVisible()
    await expect(page.getByText('png, jpg, gif, webp or bmp')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Play', exact: true })).toBeDisabled()
    await expect(page.getByText('describe-answer takes an image with its input: choose one to play it.')).toBeVisible()
  },
  review: async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Reviews' })).toHaveAttribute('aria-pressed', 'true')
    await expect(page.getByRole('textbox', { name: 'A merge request' })).toHaveValue('acme/widget#12')
    await expect(page.getByRole('button', { name: 'Review', exact: true })).toBeVisible()
    await expect(page.getByText('Lenses: security', { exact: true })).toBeVisible()
    await expect(page.getByText('Model: the chat role')).toBeVisible()
    await expect(page.getByText('apogee review mr acme/widget#12 --skills security --dry-run --output-format json')).toBeVisible()
    if (apogee === 'installed') {
      await expect(page.getByRole('heading', { name: 'Thumbnails', level: 2 })).toBeVisible()
      await expect(page.getByText('github.com/acme/widget#12 · head cafe2222 · full')).toBeVisible()
      await expect(page.getByText('lenses: security (named) · reviewer')).toBeVisible()
      await expect(page.getByText('dry run: nothing posted')).toBeVisible()
      await expect(page.getByRole('button', { name: 'Post…' })).toBeVisible()
      await expect(page.getByText('The file name from the request reaches a shell command.')).toBeVisible()
      await expect(page.getByText('candidates 1 · off change 0 · not new 0 · known 0 · duplicates 0 · dismissed 0 · tightened 0')).toBeVisible()
    } else if (apogee === 'missing') {
      await expect(page.getByText("Apogee isn't installed, so there is nothing to review with.")).toBeVisible()
    }
  },
  'review:post': async (page, apogee) => {
    if (apogee !== 'installed') return
    await expect(confirmHost(page).getByRole('heading', { name: 'Post the review to github.com/acme/widget#12?' })).toBeVisible()
    await expect(confirmHost(page)).toContainText('The dry run found 1 finding to post inline. Posting runs the review again with --yes')
    await expect(confirmHost(page)).toContainText('apogee review mr acme/widget#12 --skills security --yes --output-format json')
    await expect(confirmHost(page).getByRole('button', { name: 'Cancel' })).toBeVisible()
    await expect(confirmHost(page).getByRole('button', { name: 'Post', exact: true })).toBeVisible()
  },
  'review:targets': async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Targets and sweeps' })).toHaveAttribute('aria-pressed', 'true')
    await expect(page.getByRole('button', { name: 'Add target…' })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Sweep…' })).toBeVisible()
    if (apogee === 'missing') {
      await expect(page.getByRole('button', { name: 'Add target…' })).toBeDisabled()
      await expect(page.getByText("Apogee isn't installed, so there is nothing to review with.")).toBeVisible()
      return
    }
    await expect(page.getByRole('heading', { name: 'The last sweep', level: 2 })).toBeVisible()
    await expect(page.getByText('dry run · 1 reviewed · 0 posted · 0 deferred · 0 errors · started 2026-10-21T06:09:25Z')).toBeVisible()
    await expect(page.getByText('widget · repo · grant none')).toBeVisible()
    await expect(page.getByText('watermark none → 2026-10-18T09:00:00Z')).toBeVisible()
    await expect(page.getByText('github.com/acme/widget#12 -- Thumbnails: 1 finding; dry run: nothing posted')).toBeVisible()
    await expect(page.getByText('kept at ~/.apogee/review/sweeps/20261021T060925Z.json')).toBeVisible()
    if (apogee === 'installed') {
      await expect(page.getByText('github · owner acme · lenses by the change')).toBeVisible()
      await expect(page.getByText('posts when swept')).toBeVisible()
      await expect(page.getByText('github · repository acme/widget · lenses security')).toBeVisible()
      await expect(page.getByText('documents only')).toBeVisible()
      await expect(page.getByRole('button', { name: /^Remove/ })).toHaveCount(2)
    } else {
      await expect(page.getByText(`The targets could not be read: apogee review: ${typoWords}`)).toBeVisible()
    }
  },
  'review:skills': async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Skills' })).toHaveAttribute('aria-pressed', 'true')
    // The CLI reads no config for its skills, so a config typo lists them as installed does, as the old prototype draws it.
    if (apogee !== 'missing') {
      await expect(page.getByRole('navigation', { name: 'Skills' }).getByRole('button')).toHaveCount(7)
      await expect(page.getByRole('navigation', { name: 'Skills' }).getByRole('button', { name: /^security/ })).toHaveAttribute('aria-current', 'true')
      await expect(page.getByRole('heading', { name: 'security', level: 2 })).toBeVisible()
      await expect(page.getByText('Applies to every file.')).toBeVisible()
      await expect(page.getByText('apogee review skills show security')).toBeVisible()
      await expect(page.getByText(/^You are reviewing one change through one lens: security\./)).toBeVisible()
    } else {
      await expect(page.getByText('Every lens a review can run, shipped and your own. Choose one to read its prompt.')).toBeVisible()
    }
  },
}

for (const [preset, frame] of Object.entries(libraryFrames)) {
  for (const apogee of ['installed', 'missing', 'config-typo'] as const) {
    test(`${preset} at ${apogee}: its frame, still, its regions, tabs, controls and words, with no console error and no request but the file (design 1n)`, async ({ page }) => {
      const { errors, requests } = watch(page)
      const url = `${built}?start=${preset}&still=true&apogee=${apogee}`
      await page.goto(url)
      const place = preset.startsWith('library') ? 'Library' : 'Review'
      await expect(page.getByRole('heading', { name: place, level: 1 })).toBeVisible()
      await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: place === 'Library' ? 'Library ⌘0' : 'Review ⇧⌘R' })).toHaveAttribute('aria-current', 'page')
      await frame(page, apogee)
      expect(errors).toEqual([])
      expect(requests).toEqual([url])
    })
  }
}

test('live from the file: Close over typing asks; Keep editing keeps it; Close again and Discard closes the editor (design 1n)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=library:unsaved`)
  await confirmHost(page).getByRole('button', { name: 'Keep editing' }).click()
  await expect(confirmHost(page)).toHaveCount(0)
  const file = editorSheet(page).getByRole('textbox', { name: 'The file' })
  await expect(file).toHaveValue(/File each issue under one area\.\n$/)
  await file.press('End')
  await file.pressSequentially('More.')
  await editorSheet(page).getByRole('button', { name: 'Close' }).click()
  await confirmHost(page).getByRole('button', { name: 'Discard' }).click()
  await expect(editorSheet(page)).toHaveCount(0)
  // Edit opens the prompt the binary scaffolded again, as the file still holds it.
  await page.getByRole('button', { name: 'Edit prompts/triage.txt' }).click()
  await expect(editorSheet(page).getByRole('textbox', { name: 'The file' })).toHaveValue(/^You are triage -- Sort incoming issues by area\.\n/)
  await expect(editorSheet(page).getByRole('button', { name: 'Save' })).toBeDisabled()
  expect(errors).toEqual([])
})

test('live from the file: a save holds the editor read-only until apogee’s word, 4 ticks later; after a refused save, Put it back as it was closes once apogee has read the original (design 1n)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=library`)
  await page.getByRole('button', { name: 'Edit prompts/triage.txt' }).click()
  const file = editorSheet(page).getByRole('textbox', { name: 'The file' })
  await file.press('End')
  await file.pressSequentially('X')
  await editorSheet(page).getByRole('button', { name: 'Save' }).click()
  await expect(file).toBeDisabled()
  await expect(editorSheet(page).getByText("Saving: the editor waits for apogee's word on the file, then takes your typing again.")).toBeVisible()
  await expect(editorSheet(page).getByText("Saved. Apogee checks an agent's prompt and schema when it runs -- its first run says what is wrong, if anything.")).toBeVisible({ timeout: 5_000 })
  await expect(file).toBeEnabled()
  await editorSheet(page).getByRole('button', { name: 'Close' }).click()
  await expect(editorSheet(page)).toHaveCount(0)
  await expect(confirmHost(page)).toHaveCount(0)
  await page.goto(`${built}?start=library:refused`)
  await confirmHost(page).getByRole('button', { name: 'Put it back as it was' }).click()
  await expect(confirmHost(page)).toHaveCount(0)
  await expect(editorSheet(page).getByText("Saving: the editor waits for apogee's word on the file, then takes your typing again.")).toBeVisible()
  await expect(editorSheet(page)).toHaveCount(0, { timeout: 5_000 })
  expect(errors).toEqual([])
})

test('live from the file: after a refused save, Keep editing and Save the same text -- refused again 4 ticks later, as the old save judges the text -- and Close asks again (design 1n)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=library:refused`)
  await confirmHost(page).getByRole('button', { name: 'Keep editing' }).click()
  await expect(confirmHost(page)).toHaveCount(0)
  await editorSheet(page).getByRole('button', { name: 'Save' }).click()
  const saving = editorSheet(page).getByText("Saving: the editor waits for apogee's word on the file, then takes your typing again.")
  await expect(saving).toBeVisible()
  await expect(saving).toHaveCount(0, { timeout: 5_000 })
  await expect(editorSheet(page).getByText(/^apogee symphonies: .*unknown key 'temprature'/)).toBeVisible()
  await expect(editorSheet(page).getByText('Saved, and apogee reads it.')).toHaveCount(0)
  await editorSheet(page).getByRole('button', { name: 'Close' }).click()
  await expect(confirmHost(page).getByRole('button', { name: 'Put it back as it was' })).toBeVisible()
  expect(errors).toEqual([])
})

test('the add-target sheet shows its `apogee review targets add …` line live, the lenses sorted, Add off without a name; Add puts the row on the targets tab (design 1n)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=review:targets&still=true`)
  await page.getByRole('button', { name: 'Add target…' }).click()
  const sheet = editorSheet(page)
  await expect(sheet.getByRole('heading', { name: 'Add a sweep target' })).toBeVisible()
  await expect(sheet.getByText('apogee review targets add gadget --repo acme/gadget', { exact: true })).toBeVisible()
  await sheet.getByRole('button', { name: "Lenses: as each change's files say" }).click()
  await sheet.getByRole('menuitemcheckbox', { name: 'security' }).click()
  await sheet.getByRole('menuitemcheckbox', { name: 'ai-ml' }).click()
  await expect(sheet.getByText('apogee review targets add gadget --repo acme/gadget --skills ai-ml,security', { exact: true })).toBeVisible()
  await sheet.getByRole('textbox', { name: 'Name' }).fill('')
  await expect(sheet.getByText('apogee review targets add <name> --repo acme/gadget --skills ai-ml,security', { exact: true })).toBeVisible()
  await expect(sheet.getByRole('button', { name: 'Add', exact: true })).toBeDisabled()
  await sheet.getByRole('textbox', { name: 'Name' }).fill('gadget')
  await sheet.getByRole('menuitemcheckbox', { name: 'security' }).click()
  await sheet.getByRole('menuitemcheckbox', { name: 'ai-ml' }).click()
  await sheet.getByRole('button', { name: 'Add', exact: true }).click()
  await expect(sheet).toHaveCount(0)
  await expect(page.getByText('github · repository acme/gadget · lenses by the change')).toBeVisible()
  await expect(toast(page, "added review target 'gadget' to /Users/someone/.apogee/config/config.json")).toBeVisible()
  expect(errors).toEqual([])
})

test('live from the file: a symphony row picks; Play runs the recorded play, its stage line while it plays, then its answers (design 1n)', async ({ page }) => {
  test.setTimeout(60_000)
  const { errors } = watch(page)
  await page.goto(`${built}?start=library`)
  await page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Symphonies' }).click()
  await definitions(page).getByRole('button', { name: /^describe-answer/ }).click()
  await expect(page.getByRole('heading', { name: 'describe-answer', level: 2 })).toBeVisible()
  await page.getByRole('button', { name: 'Choose an image…' }).click()
  await expect(page.getByText('storefront.png')).toBeVisible()
  await expect(page.getByRole('button', { name: 'Play', exact: true })).toBeEnabled()
  await definitions(page).getByRole('button', { name: /^critique-revise/ }).click()
  await page.getByRole('button', { name: 'Play', exact: true }).click()
  await expect(page.getByText(/^stage 1\/2 critique — asking utility \(writer\)/)).toBeVisible()
  await expect(page.getByText('revise · chat (writer)')).toBeVisible({ timeout: 20_000 })
  await expect(page.getByText('A tighter draft, for new contributors: what the tool does, then how to run it.').first()).toBeVisible()
  // Another symphony picked clears the play, as the old pick does: critique-revise comes back with no answers until Play.
  await definitions(page).getByRole('button', { name: /^digest/ }).click()
  await definitions(page).getByRole('button', { name: /^critique-revise/ }).click()
  await expect(page.getByRole('heading', { name: 'critique-revise', level: 2 })).toBeVisible()
  await expect(page.getByText('revise · chat (writer)')).toHaveCount(0)
  await expect(page.getByText('OUTPUT')).toHaveCount(0)
  expect(errors).toEqual([])
})

const knowledgeRows = (page: Page) => page.getByRole('main').getByRole('button').filter({ hasText: /rejected · ux|shipped · eng|shipped · product/ })
const collectionList = (page: Page) => page.getByRole('navigation', { name: 'Collections' })
const canvas = (page: Page) => page.getByRole('main').locator('button[aria-pressed]').filter({ hasText: /ingest\// })
const symbolPane = (page: Page) => page.getByRole('complementary', { name: 'Symbol' })

/** What each Knowledge and Code graph preset shows at its frame, still, in each mode the old prototype draws it -- its regions, tabs, controls, states and words (design 1o). */
const knowledgeFrames: Record<string, (page: Page, apogee: 'installed' | 'missing' | 'config-typo') => Promise<void>> = {
  knowledge: async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Records' })).toHaveAttribute('aria-pressed', 'true')
    await expect(page.getByRole('button', { name: 'Capture…' })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Export…' })).toBeVisible()
    await expect(page.getByRole('textbox', { name: 'Ask the records' })).toBeVisible()
    await expect(page.getByRole('combobox', { name: 'Branch' })).toHaveValue('')
    if (apogee === 'installed') {
      await expect(page.getByRole('textbox', { name: 'Ask the records' })).toHaveValue('why was the cancel button removed')
      await expect(page.getByText('3 found · lexical')).toBeVisible()
      await expect(knowledgeRows(page)).toHaveCount(3)
      await expect(knowledgeRows(page).first()).toContainText('0.596')
      await expect(knowledgeRows(page).first()).toHaveAttribute('aria-current', 'true')
      const record = page.getByRole('complementary', { name: 'Record' })
      await expect(record.getByRole('heading', { name: 'Remove the cancel button from checkout.', level: 2 })).toBeVisible()
      for (const label of ['WHY', 'BRANCH', 'DISCIPLINE', 'FROM', 'LINKED TO', 'CAPTURED', 'RAW CONVERSATION']) await expect(record.getByRole('heading', { name: label, level: 3 })).toBeVisible()
      await expect(record.getByText('meeting · Ada Lovelace')).toBeVisible()
      await expect(record.getByRole('button', { name: 'Show the conversation it was distilled from' })).toHaveAttribute('title', 'apogee knowledge info kr-20261021T045944Z-1e2354 --raw')
      for (const name of ['Link', 'Move to…', 'Delete…']) await expect(record.getByRole('button', { name, exact: true })).toBeVisible()
    } else {
      // With no apogee the list stands empty with no words, as the old has no missing state; a config typo says the CLI's refusal.
      if (apogee === 'missing') await expect(page.getByRole('main')).not.toContainText("isn't installed")
      else await expect(page.getByRole('main')).toContainText(`The records could not be read: apogee knowledge: ${typoWords}`)
      await expect(knowledgeRows(page)).toHaveCount(0)
      await expect(page.getByText('Choose a record to read its reasoning and its decision.')).toBeVisible()
    }
  },
  'knowledge:collections': async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Collections' })).toHaveAttribute('aria-pressed', 'true')
    await expect(page.getByRole('button', { name: 'Ingest…' })).toBeVisible()
    if (apogee === 'installed') {
      await expect(collectionList(page).getByRole('button')).toHaveCount(3)
      await expect(collectionList(page).getByRole('button', { name: /^notes/ })).toHaveAttribute('aria-current', 'true')
      await expect(page.getByRole('heading', { name: 'notes', level: 2 })).toBeVisible()
      for (const label of ['CHUNKS', 'TEXT INDEX', 'VECTORS']) await expect(page.getByText(label, { exact: true })).toBeVisible()
      await expect(page.getByText('none -- a text index only')).toBeVisible()
      await expect(page.getByRole('textbox', { name: 'Search notes' })).toHaveValue('how does a heat pump work')
      await expect(page.getByText('0.000  [lexical]  heat-pumps.md#0')).toBeVisible()
      await expect(page.locator('h3', { hasText: 'SOURCES · 5' })).toBeVisible()
      await expect(page.getByRole('button', { name: 'Remove…' })).toHaveCount(5)
    } else {
      if (apogee === 'missing') await expect(page.getByRole('main')).not.toContainText("isn't installed")
      else await expect(collectionList(page)).toContainText(`The collections could not be read: apogee embed: ${typoWords}`)
      await expect(collectionList(page).getByRole('button')).toHaveCount(0)
    }
  },
  'knowledge:mixed': async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Collections' })).toHaveAttribute('aria-pressed', 'true')
    if (apogee === 'installed') {
      await expect(collectionList(page).getByRole('button', { name: /^mixed/ })).toHaveAttribute('aria-current', 'true')
      await expect(page.getByRole('heading', { name: 'mixed', level: 2 })).toBeVisible()
      await expect(page.getByText("4 of 4 chunks, 8-d [mixed: 'mock-embed' and 'mock-embed-b' -- re-ingest every source with one model]")).toBeVisible()
      await expect(page.getByRole('textbox', { name: 'Search mixed' })).toHaveValue('')
      await expect(page.locator('h3', { hasText: 'SOURCES · 4' })).toBeVisible()
    } else await expect(collectionList(page).getByRole('button')).toHaveCount(0)
  },
  graph: async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Functions' })).toHaveAttribute('aria-pressed', 'true')
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Collection · notes' })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Rebuild' })).toBeVisible()
    if (apogee === 'installed') {
      await expect(page.getByText('~/src · 4 files · 16 symbols')).toBeVisible()
      await expect(page.getByRole('button', { name: 'More' })).toBeEnabled()
      await expect(page.getByRole('navigation', { name: 'Files' }).getByRole('button', { name: 'runner.py' })).toBeVisible()
      await expect(canvas(page)).toHaveCount(6)
      await expect(symbolPane(page).getByRole('heading', { name: 'Runner.run_batch', level: 2 })).toBeVisible()
      await expect(symbolPane(page).getByText('def run_batch(self, batch)')).toBeVisible()
      await expect(symbolPane(page).getByRole('heading', { name: 'CALLED BY · 0' })).toBeVisible()
      await expect(symbolPane(page).getByRole('heading', { name: 'CALLS · 1' })).toBeVisible()
      await expect(symbolPane(page).getByRole('button', { name: 'Ask about this' })).toBeVisible()
      await expect(symbolPane(page).getByRole('button', { name: 'Open in editor' })).toBeVisible()
    } else {
      if (apogee === 'missing') await expect(page.getByRole('main')).not.toContainText("isn't installed")
      else await expect(page.getByRole('main')).toContainText(`apogee graph: ${typoWords}`)
      await expect(canvas(page)).toHaveCount(0)
      await expect(page.getByRole('button', { name: 'More' })).toBeDisabled()
    }
  },
  'graph:prose': async (page, apogee) => {
    await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Collection · notes' })).toHaveAttribute('aria-pressed', 'true')
    if (apogee === 'installed') {
      await expect(page.getByText('notes · 4 entities · 5 relations · 1 community')).toBeVisible()
      await expect(page.getByRole('button', { name: 'Find communities' })).toBeVisible()
      await expect(page.getByText(/^Graph "notes" holds 4 entities and 5 relations\./)).toBeVisible()
      for (const label of ['COMMUNITIES · 1', 'DECISIONS · 1', 'BUSIEST']) await expect(page.getByRole('heading', { name: label })).toBeVisible()
      const entity = page.getByRole('complementary', { name: 'Entity' })
      await expect(entity.getByRole('heading', { name: 'Heat pump', level: 2 })).toBeVisible()
      await expect(entity.getByText('1 in · 2 out')).toBeVisible()
      for (const label of ['COMMUNITIES', 'DECISIONS', 'CONCERNS (IN)', 'CONTAINS']) await expect(entity.getByRole('heading', { name: label, exact: true })).toBeVisible()
    } else await expect(page.getByRole('button', { name: 'Find communities' })).toHaveCount(0)
  },
  'graph:refused': async (page, apogee) => {
    if (apogee === 'installed') {
      await expect(confirmHost(page).getByRole('heading', { name: "That didn't work" })).toBeVisible()
      await expect(confirmHost(page)).toContainText("apogee graph: no node named 'ingest.runner.Runner._fetch' in graph 'demo' -- near matches: function:ingest.runner.Runner._fetch_one (ingest/runner.py:12)")
      await expect(confirmHost(page).getByRole('button')).toHaveCount(1)
      await expect(canvas(page).filter({ hasText: 'Runner._fetch' })).toHaveAttribute('aria-pressed', 'true')
      await expect(symbolPane(page)).toHaveCount(0)
    } else await expect(confirmHost(page)).toHaveCount(0)
  },
}

for (const [preset, frame] of Object.entries(knowledgeFrames)) {
  for (const apogee of ['installed', 'missing', 'config-typo'] as const) {
    test(`${preset} at ${apogee}: its frame, still, its regions, tabs, controls and words, with no console error and no request but the file (design 1o)`, async ({ page }) => {
      const { errors, requests } = watch(page)
      const url = `${built}?start=${preset}&still=true&apogee=${apogee}`
      await page.goto(url)
      const place = preset.startsWith('knowledge') ? 'Knowledge' : 'Code graph'
      await expect(page.getByRole('heading', { name: place, level: 1 })).toBeVisible()
      await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: place === 'Knowledge' ? 'Knowledge ⌘8' : 'Code graph ⌘5' })).toHaveAttribute('aria-current', 'page')
      await frame(page, apogee)
      expect(errors).toEqual([])
      expect(requests).toEqual([url])
    })
  }
}

test('live from the file: the Knowledge tabs switch; a branch filters and clears the question; Ask with nothing typed asks the old question; the raw conversation shows and hides; notes current, searched with nothing typed; another collection’s search the old toast (design 1o)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=knowledge`)
  const record = page.getByRole('complementary', { name: 'Record' })
  await record.getByRole('button', { name: 'Show the conversation it was distilled from' }).click()
  await expect(record.getByText('Ada: testers keep hitting cancel when they mean back. Grace: then we drop the cancel button from checkout. Ada: agreed, ship it.')).toBeVisible()
  await record.getByRole('button', { name: 'Hide' }).click()
  await expect(record.getByRole('button', { name: 'Show the conversation it was distilled from' })).toBeVisible()
  await page.getByRole('combobox', { name: 'Branch' }).selectOption('rejected')
  await expect(knowledgeRows(page)).toHaveCount(1)
  await expect(page.getByText('3 found · lexical')).toHaveCount(0)
  await page.getByRole('combobox', { name: 'Branch' }).selectOption('superseded')
  await expect(page.getByText('No superseded records.')).toBeVisible()
  await page.getByRole('combobox', { name: 'Branch' }).selectOption('')
  // Ask with nothing typed asks the old question, filling the field, as the old kno.ask does.
  await expect(page.getByRole('textbox', { name: 'Ask the records' })).toHaveValue('')
  await page.getByRole('button', { name: 'Ask', exact: true }).click()
  await expect(page.getByRole('textbox', { name: 'Ask the records' })).toHaveValue('why was the cancel button removed')
  await expect(page.getByText('3 found · lexical')).toBeVisible()
  await record.getByRole('button', { name: 'Link', exact: true }).click()
  await expect(toast(page, 'apogee knowledge link (prototype)')).toBeVisible()
  await page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Collections' }).click()
  await expect(page.getByRole('button', { name: 'Ingest…' })).toBeVisible()
  // With none chosen notes' page is shown and its row current, as the old kn.coll starts at notes.
  await expect(collectionList(page).getByRole('button', { name: /^notes/ })).toHaveAttribute('aria-current', 'true')
  await collectionList(page).getByRole('button', { name: /^notes/ }).click()
  // Search with nothing typed searches the old question, filling the field, as the old kno.search does.
  await page.getByRole('button', { name: 'Search', exact: true }).click()
  await expect(page.getByRole('textbox', { name: 'Search notes' })).toHaveValue('how does a heat pump work')
  await expect(page.getByText('0.000  [lexical]  solar.md#0')).toBeVisible()
  await collectionList(page).getByRole('button', { name: /^knowledge/ }).click()
  await expect(page.getByText('kr-20261021T045954Z-0084c5')).toBeVisible()
  await page.getByRole('button', { name: 'Search', exact: true }).click()
  await expect(toast(page, 'apogee embed query knowledge: its lines, as printed (prototype)')).toBeVisible()
  // The tab is kept across a visit elsewhere, as the old kn.tab is.
  await page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Code graph ⌘5' }).click()
  await page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Knowledge ⌘8' }).click()
  await expect(page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Collections' })).toHaveAttribute('aria-pressed', 'true')
  expect(errors).toEqual([])
})

test('live from the file: Files switches to IMPORTED BY and IMPORTS; a neighbour walks; the tree picks a file; the prose graph; Ask about this lands in a new session with the file and the draft (design 1o)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=graph`)
  await symbolPane(page).getByRole('button', { name: 'Runner._fetch' }).click()
  await expect(symbolPane(page).getByRole('heading', { name: 'Runner._fetch', level: 2 })).toBeVisible()
  await expect(symbolPane(page).getByRole('heading', { name: 'CALLED BY · 1' })).toBeVisible()
  await page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Files' }).click()
  await expect(canvas(page)).toHaveCount(4)
  await expect(symbolPane(page).getByRole('heading', { name: 'IMPORTED BY · 1' })).toBeVisible()
  await expect(symbolPane(page).getByRole('heading', { name: 'IMPORTS · 1' })).toBeVisible()
  await expect(symbolPane(page).getByText('ingest/runner.py · 17 lines')).toBeVisible()
  await page.getByRole('navigation', { name: 'Files' }).getByRole('button', { name: 'cli.py' }).click()
  await expect(symbolPane(page).getByRole('heading', { name: 'IMPORTS · 2' })).toBeVisible()
  await page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Collection · notes' }).click()
  await expect(page.getByText('notes · 4 entities · 5 relations · 1 community')).toBeVisible()
  await page.getByRole('button', { name: 'Find communities' }).click()
  await expect(toast(page, 'apogee graph communities notes: the utility model summarises each group (prototype)')).toBeVisible()
  await page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Functions' }).click()
  await symbolPane(page).getByRole('button', { name: 'Ask about this' }).click()
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Sessions ⌘1' })).toHaveAttribute('aria-current', 'page')
  await expect(page.getByRole('textbox', { name: /Message/ })).toHaveValue('Explain Runner.run_batch and what depends on it.')
  await expect(page.getByText('/Users/someone/src/ingest/runner.py').first()).toBeVisible()
  expect(errors).toEqual([])
})

test('live from the file: the More menu through the scrim; the save panel refusing an empty name and Save saying the CLI’s line; dedupe Done on demo, Merge them on notes to its merged note; delete asked, the graph gone, Rebuild bringing it back (design 1o)', async ({ page }) => {
  test.setTimeout(60_000)
  const { errors } = watch(page)
  await page.goto(`${built}?start=graph`)
  await page.getByRole('button', { name: 'More' }).click()
  await expect(page.getByRole('menu', { name: 'More' }).getByRole('menuitem')).toHaveText(['Export as HTML page…', 'Export as GraphML…', 'Export as Mermaid diagram…', 'Save the report…', 'Dedupe…', 'Delete the graph…'])
  await page.getByRole('button', { name: 'Close menu' }).click()
  await expect(page.getByRole('menu', { name: 'More' })).toHaveCount(0)
  await page.getByRole('button', { name: 'More' }).click()
  await page.getByRole('menuitem', { name: 'Export as HTML page…' }).click()
  const save = page.getByRole('dialog', { name: 'Save As' })
  await expect(save.getByText('apogee graph export html --graph demo -o /Users/someone/Documents/demo.html')).toBeVisible()
  await save.getByRole('textbox', { name: 'Save As' }).fill('')
  await expect(save.getByRole('button', { name: 'Save' })).toBeDisabled()
  await save.getByRole('textbox', { name: 'Save As' }).fill('demo.html')
  await save.getByRole('button', { name: 'Save' }).click()
  await expect(save).toHaveCount(0)
  await expect(toast(page, 'Exported graph "demo" to /Users/someone/Documents/demo.html: 9 entities and 12 relations drawn, 41 KB.')).toBeVisible()
  await page.getByRole('button', { name: 'More' }).click()
  await page.getByRole('menuitem', { name: 'Dedupe…' }).click()
  let dedupe = page.getByRole('dialog', { name: 'Dedupe demo' })
  await expect(dedupe.getByText(/^Code entities: 9 -- merged by exact qualified name/)).toBeVisible()
  await expect(dedupe.getByRole('button', { name: 'Merge them' })).toHaveCount(0)
  await dedupe.getByRole('button', { name: 'Done' }).click()
  await page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Collection · notes' }).click()
  await page.getByRole('button', { name: 'More' }).click()
  await page.getByRole('menuitem', { name: 'Dedupe…' }).click()
  dedupe = page.getByRole('dialog', { name: 'Dedupe notes' })
  await expect(dedupe.getByText('Would merge into Compressor (component): compressor unit', { exact: false })).toBeVisible()
  await dedupe.getByRole('button', { name: 'Merge them' }).click()
  await expect(dedupe.getByText('merging')).toBeVisible()
  await expect(dedupe.getByText(/^Merged into Compressor \(component\): compressor unit/)).toBeVisible({ timeout: 10_000 })
  await dedupe.getByRole('button', { name: 'Done' }).click()
  await page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Functions' }).click()
  await page.getByRole('button', { name: 'More' }).click()
  await page.getByRole('menuitem', { name: 'Delete the graph…' }).click()
  await expect(confirmHost(page).getByRole('heading', { name: 'Delete the graph demo?' })).toBeVisible()
  await confirmHost(page).getByRole('button', { name: 'Delete the graph' }).click()
  await expect(page.getByText('demo has no graph built. Rebuild parses its sources again -- no model, no key, no network.')).toBeVisible()
  await expect(page.getByText('0 nodes · 0 edges · 0 files')).toBeVisible()
  await expect(page.getByRole('button', { name: 'More' })).toBeDisabled()
  await page.getByRole('button', { name: 'Rebuild' }).click()
  await expect(page.getByText('~/src · 4 files · 16 symbols')).toBeVisible({ timeout: 10_000 })
  await expect(canvas(page)).toHaveCount(6)
  expect(errors).toEqual([])
})

test('live from the file: graph:refused’s OK clears the selection, the picture kept; a click on Runner, Runner.run_batch or a file asks the CLI and shows its card (design 1o)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=graph:refused`)
  await confirmHost(page).getByRole('button', { name: 'OK' }).click()
  await expect(confirmHost(page)).toHaveCount(0)
  await expect(canvas(page)).toHaveCount(6)
  await expect(canvas(page).and(page.locator('[aria-pressed="true"]'))).toHaveCount(0)
  await canvas(page).filter({ hasText: 'ingest/runner.py:4' }).click()
  await expect(symbolPane(page).getByRole('heading', { name: 'Runner', level: 2 })).toBeVisible()
  // Only Runner._fetch is refused, as the old pickNode refuses it alone: every other click shows its card, in Functions and in Files.
  await canvas(page).filter({ hasText: 'ingest/runner.py:8' }).click()
  await expect(symbolPane(page).getByRole('heading', { name: 'Runner.run_batch', level: 2 })).toBeVisible()
  await page.getByRole('group', { name: 'Show' }).getByRole('button', { name: 'Files' }).click()
  await page.getByRole('navigation', { name: 'Files' }).getByRole('button', { name: 'cli.py' }).click()
  await expect(symbolPane(page).getByRole('heading', { name: 'ingest/cli.py', level: 2 })).toBeVisible()
  await expect(confirmHost(page)).toHaveCount(0)
  expect(errors).toEqual([])
})

// Design 1p: Suites and Tasks.
const suitesList = (page: Page) => page.getByRole('navigation', { name: 'Suites' })
const tasksList = (page: Page) => page.getByRole('navigation', { name: 'Tasks' })
const sheetHost = (page: Page) => page.getByRole('dialog')
const refusedStart = "apogee task: --allow write_file: the config says permissions.write_file: deny -- a task's grant is never wider than the config"
const refusedResume = "apogee task: task task-20261109-021236 cannot resume: --allow write_file: the config says permissions.write_file: deny -- a task's grant is never wider than the config (its grants are held to the config and its agent as they are now)"

/** What a Tasks page shows at every frame that draws one: its sections, in the old words. */
async function taskSections(page: Page): Promise<void> {
  for (const label of ['BUDGET', 'PLAN · FROM THE PLAN TURN', 'ROUNDS', 'CHECKS · WHAT DONE MEANS', 'RECENT ACTIVITY']) await expect(page.getByRole('heading', { name: label, exact: true })).toBeVisible()
  for (const label of ['Rounds', 'Time so far', 'Tokens · local, no charge']) await expect(page.getByText(label, { exact: true })).toBeVisible()
}

const suitesFrames: Record<string, (page: Page, apogee: 'installed' | 'missing' | 'config-typo') => Promise<void>> = {
  suites: async (page, apogee) => {
    await expect(page.getByRole('button', { name: 'New suite…' })).toBeVisible()
    if (apogee === 'installed') {
      await expect(suitesList(page).getByRole('button')).toHaveCount(5)
      await expect(suitesList(page).getByRole('button', { name: /^fast/ })).toHaveAttribute('aria-current', 'true')
      await expect(suitesList(page).getByRole('button', { name: /^fast/ })).toContainText('default')
      await expect(suitesList(page).getByRole('button', { name: /^vast/ })).toContainText('it does not fit')
      await expect(page.getByRole('heading', { name: 'fast', level: 2 })).toBeVisible()
      for (const label of ['Stop using it by default', 'Open a chat under it', 'Switch the session to it']) await expect(page.getByRole('button', { name: label, exact: true })).toBeVisible()
      await expect(page.getByText('suite fast: no member holds memory on this machine')).toBeVisible()
      for (const label of ['ROLE', 'MODEL', 'WINDOW', 'HOLDS']) await expect(page.getByText(label, { exact: true })).toBeVisible()
      await expect(page.getByText('fastroot', { exact: true })).toBeVisible()
      await expect(page.getByText("apogee's own verdict, from models status --suite fast: the one chat --suite admits a session by, refusing a suite that does not fit unless forced.")).toBeVisible()
    } else {
      await expect(suitesList(page).getByRole('button')).toHaveCount(0)
      if (apogee === 'missing') await expect(page.getByRole('main')).not.toContainText("isn't installed")
      else await expect(suitesList(page)).toContainText(`apogee config: ${typoWords}`)
    }
  },
}

const tasksFrames: Record<string, (page: Page, apogee: 'installed' | 'missing' | 'config-typo') => Promise<void>> = {
  tasks: async (page, apogee) => {
    if (apogee === 'installed') {
      await expect(tasksList(page).getByRole('button')).toHaveCount(4)
      await expect(tasksList(page).getByRole('button').first()).toHaveAttribute('aria-current', 'true')
      await expect(page.getByRole('heading', { name: 'say hello', level: 2 })).toBeVisible()
      await expect(page.getByText('running · round 1 of 2').first()).toBeVisible()
      for (const label of ['Stop after this round', 'Stop now']) await expect(page.getByRole('button', { name: label })).toBeVisible()
      await expect(page.getByText('in /Users/someone/work · started Oct 21 01:14 · nothing that writes granted · task-20261021-011456')).toBeVisible()
      await taskSections(page)
      await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Tasks ⌘4 · 1 running' })).toBeVisible()
    } else {
      await expect(tasksList(page).getByRole('button')).toHaveCount(0)
      if (apogee === 'missing') {
        await expect(page.getByRole('heading', { level: 2 })).toHaveCount(0)
        await expect(page.getByRole('main')).not.toContainText("isn't installed")
      } else {
        await expect(tasksList(page)).toContainText(`The tasks could not be read: apogee task: ${typoWords}`)
        // The run the frame starts is refused by the config before it begins: the alert says so, in the CLI's words.
        await expect(confirmHost(page)).toContainText(`apogee task: ${typoWords.replace(' (exited with status 1)', '')}`)
      }
    }
  },
  'tasks:denied': async (page, apogee) => {
    if (apogee !== 'installed') return
    await expect(page.getByRole('heading', { name: 'Write the report', level: 2 })).toBeVisible()
    await expect(page.getByText('exhausted · 1 of 1 rounds used').first()).toBeVisible()
    await expect(page.getByRole('button', { name: 'Run again with 2 rounds' })).toBeVisible()
    for (const label of ['GRANTS', 'ALLOWED', 'DENIED']) await expect(page.getByRole('heading', { name: label, exact: true })).toBeVisible()
    await expect(page.getByText("round 1: run_command on ls -- this task's grant")).toBeVisible()
    await expect(page.getByText('round 1: write_file on report.txt -- nobody present to allow it')).toBeVisible()
    await expect(page.getByText('denied: write_file on report.txt -- nobody present to allow it')).toBeVisible()
    await taskSections(page)
  },
  'tasks:more': async (page, apogee) => {
    if (apogee !== 'installed') return
    await expect(tasksList(page).getByRole('button', { name: /say hello|Say done|Answer|Find|Write/ })).toHaveCount(51)
    await expect(tasksList(page)).toContainText('The newest 50 of 60 tasks ·')
    await expect(tasksList(page).getByRole('button', { name: 'Show all' })).toBeVisible()
  },
  'tasks:resuming': async (page, apogee) => {
    if (apogee !== 'installed') return
    await expect(page.getByRole('heading', { name: 'Find the word', level: 2 })).toBeVisible()
    await expect(page.getByRole('button', { name: 'Resuming…' })).toBeDisabled()
    for (const label of ['Stop after this round', 'Stop now']) await expect(page.getByRole('button', { name: label })).toBeVisible()
    await expect(page.getByText('in /Users/someone/work, where it was started')).toBeVisible()
  },
  'tasks:refused': async (page, apogee) => {
    if (apogee === 'missing') return expect(confirmHost(page)).toHaveCount(0)
    if (apogee === 'config-typo') return expect(confirmHost(page)).toContainText(`apogee task: ${typoWords.replace(' (exited with status 1)', '')}`)
    await expect(confirmHost(page).getByRole('heading', { name: "That didn't work" })).toBeVisible()
    await expect(confirmHost(page)).toContainText(refusedStart)
    await expect(confirmHost(page).getByRole('button')).toHaveCount(1)
    await expect(sheetHost(page)).toHaveCount(0)
    await expect(tasksList(page).getByRole('button')).toHaveCount(4)
  },
  'tasks:resumerefused': async (page, apogee) => {
    // No ledger read, no task to resume: nothing is asked of the CLI, so nothing is refused.
    if (apogee !== 'installed') return expect(confirmHost(page)).toHaveCount(0)
    await expect(confirmHost(page)).toContainText(refusedResume)
    await expect(page.getByRole('heading', { name: 'Find the word, writing notes', level: 2 })).toBeVisible()
    await expect(page.locator('.card').getByText(refusedResume)).toBeVisible()
  },
  newtask: async (page) => {
    await expect(sheetHost(page).getByRole('heading', { name: 'New task' })).toBeVisible()
    await expect(sheetHost(page).getByRole('button', { name: 'Cancel' })).toBeVisible()
    if ((await page.evaluate(() => location.search)).includes('apogee=installed')) {
      await expect(sheetHost(page).getByRole('textbox', { name: 'Goal' })).toHaveValue('Make tests/test_parsers.py pass on Python 3.12')
      await expect(sheetHost(page).getByText('apogee task run "Make tests/test_parsers.py pass on Python 3.12" --tools --allow write_file --allow run_command --suite fast --output-format stream-json')).toBeVisible()
      await expect(sheetHost(page).getByRole('button', { name: 'Start task' })).toBeEnabled()
      for (const group of ['Writing files', 'Running commands']) await expect(sheetHost(page).getByRole('group', { name: group }).getByRole('button', { name: 'Granted' })).toHaveAttribute('aria-pressed', 'true')
    }
  },
  'newtask:recipe': async (page) => {
    await expect(sheetHost(page).getByRole('heading', { name: 'New task' })).toBeVisible()
    if ((await page.evaluate(() => location.search)).includes('apogee=installed')) {
      await expect(sheetHost(page).getByRole('combobox', { name: 'Recipe' })).toHaveValue('digest-folder')
      await expect(sheetHost(page).getByRole('textbox', { name: 'Input' })).toHaveValue('docs')
      await expect(sheetHost(page).getByRole('checkbox', { name: 'Allow write_file' })).toBeChecked()
      await expect(sheetHost(page).getByText('The same run as typing')).toBeVisible()
      await expect(sheetHost(page).getByText('apogee task run --recipe digest-folder --input docs --allow write_file --suite fast --output-format stream-json')).toBeVisible()
    }
  },
}

for (const [preset, frame] of Object.entries({ ...suitesFrames, ...tasksFrames })) {
  for (const apogee of ['installed', 'missing', 'config-typo'] as const) {
    test(`${preset} at ${apogee}: its frame, still, its regions, controls and words, with no console error and no request but the file (design 1p)`, async ({ page }) => {
      const { errors, requests } = watch(page)
      const url = `${built}?start=${preset}&still=true&apogee=${apogee}`
      await page.goto(url)
      const place = preset === 'suites' ? 'Suites' : 'Tasks'
      await expect(page.getByRole('heading', { name: place, level: 1 })).toBeVisible()
      await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: new RegExp(`^${place} ⌘${place === 'Suites' ? 3 : 4}`) })).toHaveAttribute('aria-current', 'page')
      await frame(page, apogee)
      expect(errors).toEqual([])
      expect(requests).toEqual([url])
    })
  }
}

test('Suites and Tasks open by their sidebar buttons from any screen (design 1p; the browser may keep ⌘3 and ⌘4)', async ({ page }) => {
  await page.goto(`${built}?start=chat:new&still=true`)
  await page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Suites ⌘3' }).click()
  await expect(suitesList(page).getByRole('button')).toHaveCount(5)
  await page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: /^Tasks ⌘4/ }).click()
  await expect(tasksList(page).getByRole('button')).toHaveCount(3)
  await expect(page.getByRole('heading', { name: 'Answer when asked', level: 2 })).toBeVisible()
})

test('live from the file: a suite picked; over budget both actions say anyway; Use for new sessions moves the default and says so; New suite… the old toast; Open a chat under it lands on Sessions (design 1p)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=suites`)
  await suitesList(page).getByRole('button', { name: /^vast/ }).click()
  await expect(page.getByRole('button', { name: 'Open a chat under it anyway' })).toHaveAttribute('title', "apogee chat --suite vast --force: run it over this machine's memory")
  await expect(page.getByRole('button', { name: 'Switch the session to it anyway' })).toBeVisible()
  await expect(page.getByText(/^suite vast needs 247056 MiB/)).toBeVisible()
  await suitesList(page).getByRole('button', { name: /^cloud/ }).click()
  await page.getByRole('button', { name: 'Use for new sessions' }).click()
  await expect(toast(page, 'cloud is the default suite')).toBeVisible()
  await expect(suitesList(page).getByRole('button', { name: /^cloud/ })).toContainText('default')
  await expect(suitesList(page).getByRole('button', { name: /^fast/ })).not.toContainText('default')
  await expect(page.getByRole('button', { name: 'Stop using it by default' })).toBeVisible()
  await page.getByRole('button', { name: 'New suite…' }).click()
  await expect(toast(page, 'New suite: pick a main model and helpers (prototype)')).toBeVisible()
  await page.getByRole('button', { name: 'Open a chat under it', exact: true }).click()
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Sessions ⌘1' })).toHaveAttribute('aria-current', 'page')
  expect(errors).toEqual([])
})

test('live from the file: the recorded run plays through its rounds to exhausted, the sidebar’s Tasks badge gone; Run again on another exhausted task starts a run shown at once (design 1p)', async ({ page }) => {
  test.setTimeout(60_000)
  const { errors } = watch(page)
  await page.goto(`${built}?start=tasks`)
  await expect(page.getByText('exhausted · 2 of 2 rounds used').first()).toBeVisible({ timeout: 20_000 })
  await expect(page.getByRole('button', { name: 'Run again with 4 rounds' })).toBeVisible()
  await expect(page.getByText('Round 2', { exact: true })).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Tasks ⌘4' })).toBeVisible()
  await tasksList(page).getByRole('button', { name: /exhausted · 1 of 1 rounds used/ }).click()
  await page.getByRole('button', { name: 'Run again with 2 rounds' }).click()
  await expect(page.getByRole('button', { name: 'Stop now' })).toBeVisible()
  await expect(page.getByRole('navigation', { name: 'Places' }).getByRole('button', { name: 'Tasks ⌘4 · 1 running' })).toBeVisible()
  expect(errors).toEqual([])
})

test('live from the file: Stop after this round says so until its round ends, then halted with Resume -- exhausted where that round is the budget’s last; Stop now on another run ends it cancelled (design 1p)', async ({ page }) => {
  test.setTimeout(60_000)
  const { errors } = watch(page)
  await page.goto(`${built}?start=tasks`)
  await page.getByRole('button', { name: 'Stop after this round' }).click()
  const halting = page.getByText(/running · halts when (the plan|round [12]) ends/).first()
  await expect(halting).toBeVisible()
  // A halt that lands as round 2 ends, the last of the budget, ends exhausted (the record's "Where a stop lands"); one landing earlier ends halted.
  const last = (await halting.innerText()).includes('round 2')
  await expect(page.getByText(last ? 'exhausted · 2 of 2 rounds used' : 'halted · resumes from its ledger').first()).toBeVisible({ timeout: 20_000 })
  if (!last) await expect(page.getByRole('button', { name: 'Resume', exact: true })).toBeVisible()
  await page.goto(`${built}?start=tasks`)
  await page.getByRole('button', { name: 'Stop now' }).click()
  await expect(page.getByText('cancelled by you').first()).toBeVisible({ timeout: 10_000 })
  expect(errors).toEqual([])
})

test('live from the file: the stalled task’s resume starts up, then runs round 3 to done (design 1p)', async ({ page }) => {
  test.setTimeout(60_000)
  const { errors } = watch(page)
  await page.goto(`${built}?start=tasks:resuming`)
  await expect(page.getByText('done · every check passed').first()).toBeVisible({ timeout: 20_000 })
  await expect(page.getByText('Round 3', { exact: true })).toBeVisible()
  await expect(page.getByRole('button', { name: 'Resume' })).toHaveCount(0)
  expect(errors).toEqual([])
})

test('Show all lists every task of the 60; the New task sheet: the goal turns Start on, a recipe shows the same run as typing, Cancel closes it; the refused start’s alert closes on OK (design 1p)', async ({ page }) => {
  const { errors } = watch(page)
  await page.goto(`${built}?start=tasks:more&still=true`)
  await tasksList(page).getByRole('button', { name: 'Show all' }).click()
  await expect(tasksList(page).getByRole('button', { name: /say hello|Say done|Answer|Find|Write/ })).toHaveCount(61)
  await expect(tasksList(page).getByRole('button', { name: 'Show all' })).toHaveCount(0)
  await page.getByRole('button', { name: 'New task…' }).click()
  await expect(sheetHost(page).getByRole('button', { name: 'Start task' })).toBeDisabled()
  await sheetHost(page).getByRole('textbox', { name: 'Goal' }).fill('Tidy the README')
  await expect(sheetHost(page).getByRole('button', { name: 'Start task' })).toBeEnabled()
  await expect(sheetHost(page).getByText('apogee task run "Tidy the README" --tools --allow write_file --suite fast --output-format stream-json')).toBeVisible()
  await sheetHost(page).getByRole('combobox', { name: 'Recipe' }).selectOption('changelog-draft')
  await expect(sheetHost(page).getByText('The same run as typing')).toBeVisible()
  await expect(sheetHost(page).getByRole('button', { name: 'Start task' })).toBeDisabled()
  await sheetHost(page).getByRole('button', { name: 'Cancel' }).click()
  await expect(sheetHost(page)).toHaveCount(0)
  await page.goto(`${built}?start=tasks:refused&still=true`)
  await confirmHost(page).getByRole('button', { name: 'OK' }).click()
  await expect(confirmHost(page)).toHaveCount(0)
  expect(errors).toEqual([])
})
