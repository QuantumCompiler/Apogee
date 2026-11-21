/**
 * The old prototype's baseline (design 1c): still frames of the frozen
 * lib/design/spec, re-made from one recorded revision, so 1s can compare the
 * React app against it after 1t deletes the old files.
 *
 *   npm run baseline                                 # capture: re-make the images from parity/baseline.json
 *   npm run baseline -- --record --reason "<why>"    # record: take the newest commit touching lib/design/spec
 *
 * Git is only read: the revision is archived into a temp folder, never
 * checked out, and no command takes an optional lock on the shared index.
 * The archived html/ folder is served by `python3 -m http.server`, as
 * spec/README.md opens it, and Chromium (Playwright's, from 1b) shoots each
 * start preset at 1440x900 with `still=true`. Images, a hash list and a
 * contact sheet go to .parity/baseline/<revision>/, which git ignores.
 *
 * Two runs draw the same pixels because: the clock is fixed and Math.random
 * seeded; each image is shot in a page of its own, on its second load, once
 * its fonts are in the cache; Chromium rasterizes on one thread; and each
 * Google Fonts stylesheet is held to the static one recorded in
 * baseline.json (the same URL is sometimes answered with dynamically subset
 * faces, which draw differently).
 */
import { execFileSync, spawn, spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { type BrowserContext, chromium, type Page } from 'playwright'

/** The frozen prototype, relative to the repository's root. */
export const specPath = 'lib/design/spec'

/** The `apogee` prop's values (main.html's data-props), `installed` first: the others are kept only where they differ from it. */
export const modes = ['installed', 'missing', 'config-typo'] as const
export type Mode = (typeof modes)[number]

/** The `$preview` every Screen board declares. */
export const viewport = { width: 1440, height: 900 } as const

/** What makes two runs draw the same pixels: one instant, one time zone, one locale, one seed. */
export const fixed = { clock: '2026-11-14T09:41:00.000Z', timezone: 'UTC', locale: 'en-US', seed: 1 } as const

/** parity/baseline.json: the recorded revision and what a capture of it must reproduce. */
export interface Baseline {
  /** The full hash of the newest commit touching lib/design/spec when it was recorded. */
  revision: string
  /** That commit's date. */
  date: string
  /** The tree hash of lib/design/spec at the revision. */
  tree: string
  /** The `start` options in the revision's main.html data-props, in order. */
  presets: string[]
  /** Per preset, the `apogee` modes whose image is kept: `installed`, and any other that draws differently. */
  modes: Record<string, Mode[]>
  /** spec/CLAUDE.md's 30-tick check on the revision, per mode, as it prints. */
  thirtyTicks: Record<Mode, string>
  /** The Playwright version, and so the Chromium, that shoots it. */
  playwright: string
  viewport: { width: number; height: number }
  clock: string
  timezone: string
  locale: string
  seed: number
  /** Each Google Fonts stylesheet the boards link, by URL: the SHA-256 of the static one served at the record. */
  fonts: Record<string, string>
  /** Every record, oldest first, and why it was made. */
  history: { revision: string; recorded: string; reason: string }[]
}

/** A run that will not go on, and why: the message is the whole story. */
export class Refusal extends Error {
  override name = 'Refusal'
}

/** One image: its path under the revision's folder and its SHA-256. */
export interface Shot {
  file: string
  hash: string
  /** What it shows, for the contact sheet: `chat:retry · missing`, `ColorTokens · chrome=light`. */
  label: string
  kind: 'screen' | 'board'
}

/** How a run compares with the hash list already in its folder. */
export interface Comparison {
  same: number
  /** Images whose hash changed, appeared or went missing. */
  differ: string[]
}

interface Options {
  /** The repository's root. */
  repo: string
  /** parity/baseline.json. */
  baselineFile: string
  /** .parity/baseline: each revision's images go in a folder of its own beneath it. */
  outRoot: string
  log: (line: string) => void
  /** A hook on each browser context before it opens a page (the tests route the network with it). */
  prepareContext?: (context: BrowserContext) => Promise<unknown>
}

// MARK: - Reading the prototype

/** HTML's character references, decoded once, as the browser decodes an attribute. */
export function decodeEntities(text: string): string {
  const named: Record<string, string> = { amp: '&', apos: "'", quot: '"', lt: '<', gt: '>' }
  return text.replace(/&(#x[0-9a-f]+|#[0-9]+|[a-z]+);/gi, (whole, name: string) => {
    if (name[0] === '#') {
      const code = name[1] === 'x' || name[1] === 'X' ? parseInt(name.slice(2), 16) : parseInt(name.slice(1), 10)
      return String.fromCodePoint(code)
    }
    return named[name.toLowerCase()] ?? whole
  })
}

/** A Design Component's `data-props`, read as support.js reads it: from the `data-dc-script` tag, single- or double-quoted. */
export function readProps(html: string): Record<string, unknown> {
  const tag = /<script[^>]*data-dc-script[^>]*>/i.exec(html)?.[0]
  const raw = tag && (/data-props\s*=\s*'([^']*)'/.exec(tag) ?? /data-props\s*=\s*"([^"]*)"/.exec(tag))?.[1]
  if (raw === undefined || raw === '') throw new Refusal('the file has no data-dc-script tag with data-props')
  return JSON.parse(decodeEntities(raw)) as Record<string, unknown>
}

/** The enum props a board offers, in order: never `$preview`, never a boolean. */
export function propMenus(html: string): { name: string; options: string[] }[] {
  const menus: { name: string; options: string[] }[] = []
  for (const [name, prop] of Object.entries(readProps(html))) {
    if (name.startsWith('$') || typeof prop !== 'object' || prop === null) continue
    const { editor, options } = prop as { editor?: unknown; options?: unknown }
    if (editor === 'enum' && Array.isArray(options)) menus.push({ name, options: options.map(String) })
  }
  return menus
}

/** main.html's start presets: the `start` options in its data-props. */
export function readPresets(html: string): string[] {
  const start = propMenus(html).find((menu) => menu.name === 'start')
  if (!start) throw new Refusal("main.html's data-props has no start menu, so there are no presets to capture")
  return start.options
}

/**
 * spec/CLAUDE.md's "Checking a change", in one `apogee` mode: every start
 * screen 30 ticks, reporting anything that throws or reads `undefined` or
 * `NaN`, in its words.
 */
export function thirtyTicks(html: string, mode: Mode): string {
  const code = /<script[^>]*data-dc-script[^>]*>([\s\S]*?)<\/script>/.exec(html)?.[1]
  if (code === undefined) throw new Refusal('main.html has no data-dc-script')
  const starts = readPresets(html)
  class DCLogic {
    props: Record<string, unknown>
    state: Record<string, unknown> = {}
    constructor(props?: Record<string, unknown>) {
      this.props = props ?? {}
    }
    setState(patch: unknown): void {
      Object.assign(this.state, typeof patch === 'function' ? patch(this.state) : patch)
    }
  }
  type Running = { tick(): void; renderVals(): unknown }
  const Component = new Function('DCLogic', `${code};return Component;`)(DCLogic) as new (props: Record<string, unknown>) => Running
  const bad: string[] = []
  const walk = (value: unknown, at: string, depth: number): void => {
    if (depth > 9 || value == null) return
    if (typeof value === 'number' && Number.isNaN(value)) bad.push(at)
    else if (typeof value === 'string' && /undefined|NaN/.test(value)) bad.push(`${at} = ${value.slice(0, 60)}`)
    else if (typeof value === 'object') for (const key in value) walk((value as Record<string, unknown>)[key], `${at}.${key}`, depth + 1)
  }
  for (const start of starts) {
    try {
      const running = new Component({ start, still: true, apogee: mode })
      for (let i = 0; i < 30; i += 1) running.tick()
      walk(running.renderVals(), start, 0)
    } catch (error) {
      bad.push(`${start}: ${(error as Error).message}`)
    }
  }
  return `${starts.length} start screens, ${bad.length ? bad.join('\n') : 'no problems'}`
}

// MARK: - Git, read only

function git(repo: string, args: string[], input?: Buffer): Buffer {
  return execFileSync('git', ['--no-optional-locks', '-C', repo, ...args], { maxBuffer: 1 << 30, input, stdio: ['pipe', 'pipe', 'pipe'] })
}

/** The files `git status` shows under lib/design/spec: changed, staged or untracked. */
export function dirtySpec(repo: string): string[] {
  return git(repo, ['status', '--porcelain=v1', '--untracked-files=all', '--', specPath])
    .toString('utf8')
    .split('\n')
    .filter(Boolean)
    .map((line) => line.slice(3))
}

/** The newest commit that touches lib/design/spec. */
export function newestSpecCommit(repo: string): string {
  const revision = git(repo, ['log', '-1', '--format=%H', '--', specPath]).toString('utf8').trim()
  if (!revision) throw new Refusal(`no commit in ${repo} touches ${specPath}`)
  return revision
}

/** Whether the repository holds this commit. */
export function isReachable(repo: string, revision: string): boolean {
  if (!/^[0-9a-f]{40}$/.test(revision)) return false
  return spawnSync('git', ['--no-optional-locks', '-C', repo, 'cat-file', '-e', `${revision}^{commit}`], { stdio: 'ignore' }).status === 0
}

/** lib/design/spec's tree hash at the revision. */
export function specTree(repo: string, revision: string): string {
  return git(repo, ['rev-parse', `${revision}:${specPath}`]).toString('utf8').trim()
}

/** `git archive` of lib/design/spec at the revision into a folder: no checkout, no worktree entry. Returns its html/ folder. */
export function archiveSpec(repo: string, revision: string, into: string): string {
  const tar = git(repo, ['archive', '--format=tar', revision, specPath])
  execFileSync('tar', ['-x', '-C', into], { input: tar, maxBuffer: 1 << 30 })
  return join(into, specPath, 'html')
}

/** The Playwright version installed beside this script. */
export function playwrightVersion(): string {
  return (createRequire(import.meta.url)('playwright/package.json') as { version: string }).version
}

// MARK: - Shooting

/** The canvas's alias for main.html (support.js's `App` → `main.html`): its frames are main.html's presets. */
const aliases: Record<string, string> = { 'App.dc.html': 'main.html' }

/** Fonts are served from these hosts; a failure there refuses the run. */
const fontHosts = ['fonts.googleapis.com', 'fonts.gstatic.com']

/**
 * Chromium's switches. One raster thread: with several, tiles race to cache
 * a glyph, and the same web-font glyphs at the same positions came out
 * differently from one launch to the next (Inspector's Geist Mono, in about
 * one launch in four), so two runs could not match hash for hash. The
 * browser is Playwright's default headless shell, whose software raster
 * draws each edge the same way every time (the full build's GPU raster did
 * not).
 */
export const browserArgs = ['--num-raster-threads=1']

/** How many fresh contexts a shot may take before Google Fonts serves its static stylesheet. */
export const pinTries = 10

/** `chat:retry` → `chat_retry`: a file name Finder shows as written. */
export function fileName(text: string): string {
  return text.replace(/[^A-Za-z0-9._=-]/g, '_')
}

/**
 * Whether a Google Fonts stylesheet is the static one: every face from a
 * versioned file (`fonts.gstatic.com/s/<family>/v<N>/…`). The same URL is
 * sometimes answered with faces from dynamically subset files
 * (`fonts.gstatic.com/l/font?kit=…`), which draw different pixels.
 */
export function isStaticFonts(css: string): boolean {
  const sources = css.match(/url\([^)]*\)/g) ?? []
  return sources.length > 0 && sources.every((source) => /^url\(['"]?https:\/\/fonts\.gstatic\.com\/s\//.test(source))
}

/** Google Fonts answered a stylesheet with dynamically subset faces: the shot is taken again in a fresh context. */
export class Unpinned extends Error {
  readonly url: string
  constructor(url: string) {
    super(`Google Fonts served a dynamically subset stylesheet for ${url}`)
    this.url = url
  }
}

/** Serves a folder as spec/README.md does, `python3 -m http.server`, on a free port of 127.0.0.1. */
export async function serve(folder: string): Promise<{ origin: string; stop: () => void }> {
  const server = spawn('python3', ['-u', '-m', 'http.server', '0', '--bind', '127.0.0.1', '--directory', folder], {
    stdio: ['ignore', 'pipe', 'ignore'],
  })
  const port = await new Promise<number>((done, failed) => {
    let said = ''
    server.stdout.on('data', (chunk: Buffer) => {
      said += chunk.toString('utf8')
      const found = /port (\d+)/.exec(said)
      if (found) done(Number(found[1]))
    })
    server.on('error', (error) => failed(new Refusal(`python3 -m http.server did not start: ${error.message}`)))
    server.on('exit', (status) => failed(new Refusal(`python3 -m http.server exited with status ${status} before it served`)))
  })
  return { origin: `http://127.0.0.1:${port}`, stop: () => server.kill() }
}

/** The page's record of each stylesheet that finished, loaded or failed (load and error do not bubble, so it listens in capture). */
interface Settled {
  __apogeeSettled?: Set<string>
}

/**
 * Runs in every page before its scripts: Math.random seeded (mulberry32),
 * and each stylesheet that settles noted, so a failure before the wait
 * begins is still seen.
 */
export function prepareWindow(seed: number): void {
  let state = seed
  Math.random = () => {
    state = (state + 0x6d2b79f5) | 0
    let t = Math.imul(state ^ (state >>> 15), 1 | state)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
  const settled = new Set<string>()
  ;(window as unknown as Settled).__apogeeSettled = settled
  const note = (event: Event) => {
    if (event.target instanceof HTMLLinkElement) settled.add(event.target.href)
  }
  document.addEventListener('load', note, true)
  document.addEventListener('error', note, true)
}

/** A browser context, with what it has seen of the fonts. Each shot opens a page of its own in it. */
export interface Session {
  context: BrowserContext
  /** Font requests that failed during the current shot. */
  fontFailures: string[]
  /** Each Google Fonts stylesheet this context fetched, by URL: its body. The context's cache serves it from then on. */
  stylesheets: Map<string, Promise<string>>
}

/**
 * A context of its own settings (the viewport, the fixed clock, time zone,
 * locale and seed). No request is routed by the script itself: routing turns
 * the browser's cache off, and the warm second load every shot relies on
 * needs it.
 */
export async function openSession(
  browser: Awaited<ReturnType<typeof chromium.launch>>,
  settings: Pick<Baseline, 'clock' | 'timezone' | 'locale' | 'seed'>,
  prepare: Options['prepareContext'],
): Promise<Session> {
  const context = await browser.newContext({
    viewport,
    deviceScaleFactor: 1,
    timezoneId: settings.timezone,
    locale: settings.locale,
    colorScheme: 'light',
  })
  await context.clock.setFixedTime(new Date(settings.clock))
  await context.addInitScript(prepareWindow, settings.seed)
  if (prepare) await prepare(context)
  const session: Session = { context, fontFailures: [], stylesheets: new Map() }
  const host = (url: string) => new URL(url).hostname
  context.on('requestfailed', (request) => {
    if (fontHosts.includes(host(request.url()))) session.fontFailures.push(`${request.url()}: ${request.failure()?.errorText ?? 'failed'}`)
  })
  context.on('response', (response) => {
    const url = response.url()
    if (!fontHosts.includes(host(url))) return
    if (response.status() >= 400) session.fontFailures.push(`${url}: HTTP ${response.status()}`)
    else if (host(url) === 'fonts.googleapis.com' && !session.stylesheets.has(url)) {
      session.stylesheets.set(url, response.body().then((body) => body.toString('utf8'), () => ''))
    }
  })
  return session
}

/** Navigates, waits for support.js to draw, for every stylesheet, font and image, and reports what it found. */
export async function load(page: Page, url: string, what: string): Promise<{ errorBox: string | null; unloaded: string[]; failedFaces: string[]; googleSheets: string[] }> {
  await page.goto(url, { waitUntil: 'load' })
  try {
    await page.waitForFunction(() => !document.querySelector('x-dc'), undefined, { timeout: 30_000 })
  } catch {
    throw new Refusal(`${what} never drew: support.js did not replace its <x-dc> template within 30 seconds (${url})`)
  }
  return page.evaluate(async () => {
    const links = [...document.querySelectorAll<HTMLLinkElement>('link[rel="stylesheet"]')]
    const settled = (window as unknown as Settled).__apogeeSettled ?? new Set<string>()
    await Promise.all(
      links.map((link) =>
        link.sheet || settled.has(link.href)
          ? undefined
          : new Promise((done) => {
              link.addEventListener('load', done, { once: true })
              link.addEventListener('error', done, { once: true })
              setTimeout(done, 20_000)
            }),
      ),
    )
    const settle = () => new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)))
    await document.fonts.ready
    await settle()
    await document.fonts.ready
    await Promise.all([...document.images].map((image) => (image.complete ? undefined : image.decode().catch(() => undefined))))
    // support.js's fail() (html/support.js): a <pre> with its own inline border, margin and wrapping.
    const box = [...document.querySelectorAll('pre')].find(
      (pre) => pre.style.borderTopColor === 'rgb(242, 131, 111)' && pre.style.margin === '24px' && pre.style.whiteSpace === 'pre-wrap',
    )
    return {
      errorBox: box?.textContent ?? null,
      unloaded: links.filter((link) => !link.sheet).map((link) => link.href),
      failedFaces: [...document.fonts].filter((face) => face.status === 'error').map((face) => face.family),
      googleSheets: links.map((link) => link.href).filter((href) => new URL(href).hostname === 'fonts.googleapis.com'),
    }
  })
}

/**
 * Refuses a font that did not load, then holds each Google Fonts stylesheet
 * to its pin: a dynamically subset one is `Unpinned` (taken again in a fresh
 * context); a static one is recorded when recording, and otherwise must
 * hash to what baseline.json recorded.
 */
export async function checkFonts(session: Session, found: Awaited<ReturnType<typeof load>>, what: string, pins: Pins): Promise<void> {
  const fonts = [
    ...session.fontFailures,
    ...found.unloaded.map((href) => `${href}: the stylesheet did not load`),
    ...found.failedFaces.map((family) => `${family}: the font face failed`),
  ]
  if (fonts.length) {
    throw new Refusal(
      `the fonts did not load for ${what}, and a baseline is never recorded in fallback fonts (the boards link fonts.googleapis.com, so a capture needs the network): ${[...new Set(fonts)].join('; ')}`,
    )
  }
  for (const href of found.googleSheets) {
    const body = session.stylesheets.get(href)
    if (body === undefined) throw new Refusal(`${what} links ${href}, and the capture saw no response for it`)
    const css = await body
    if (!isStaticFonts(css)) throw new Unpinned(href)
    const hash = sha256(Buffer.from(css, 'utf8'))
    const pinned = pins.fonts[href]
    if (pinned === undefined && pins.recording) pins.fonts[href] = hash
    else if (pinned === undefined) {
      throw new Refusal(`${what} links ${href}, which baseline.json's fonts do not name: re-record with a reason`)
    } else if (pinned !== hash) {
      throw new Refusal(
        `Google Fonts now serves a different stylesheet for ${href} (sha256 ${hash}; baseline.json recorded ${pinned}): the fonts changed since the record, so its images cannot be re-made exactly. Re-record with a reason.`,
      )
    }
  }
}

/** The stylesheets baseline.json pins by hash, and whether this run records them. */
export interface Pins {
  fonts: Record<string, string>
  recording: boolean
}

/**
 * One image, in a page of its own (so a renderer's caches never carry one
 * shot's glyphs into the next): loads the page once so it fetches its fonts,
 * holds the fonts to their pins, loads it again so it draws with every font
 * already in the context's cache, refuses a broken render, and shoots it.
 */
async function shoot(session: Session, pins: Pins, url: string, what: string, size: { width: number; height: number }, fullPage: boolean): Promise<Buffer> {
  session.fontFailures.length = 0
  const page = await session.context.newPage()
  try {
    await page.setViewportSize(size)
    await checkFonts(session, await load(page, url, what), what, pins)
    const found = await load(page, url, what)
    if (found.errorBox !== null) {
      throw new Refusal(`${what} drew support.js's error box, so the baseline would record a broken render: ${found.errorBox}`)
    }
    await checkFonts(session, found, what, pins)
    return await page.screenshot({ fullPage, animations: 'disabled', caret: 'hide', scale: 'css' })
  } finally {
    await page.close()
  }
}

export function sha256(bytes: Buffer): string {
  return createHash('sha256').update(bytes).digest('hex')
}

/**
 * Shoots an archived html/ folder: every preset in every mode, keeping a
 * mode's image only where it differs from `installed`, then each board
 * outside the screens once per value of its prop menus, full page at its
 * canvas.json size. Writes the images under `outDir`.
 */
async function shootAll(
  html: string,
  baseline: Pick<Baseline, 'presets' | 'clock' | 'timezone' | 'locale' | 'seed'>,
  pins: Pins,
  outDir: string,
  options: Pick<Options, 'log' | 'prepareContext'>,
): Promise<{ shots: Shot[]; modes: Record<string, Mode[]> }> {
  const canvas = JSON.parse(readFileSync(join(html, '../json/canvas.json'), 'utf8')) as {
    boards: Record<string, { w: number; h: number }>
  }
  const names = new Map<string, string>()
  const claim = (file: string, what: string) => {
    const other = names.get(file)
    if (other !== undefined) throw new Refusal(`${what} and ${other} would both be saved as ${file}`)
    names.set(file, what)
    return file
  }
  const server = await serve(html)
  const browser = await chromium.launch({ args: browserArgs })
  const shots: Shot[] = []
  const kept: Record<string, Mode[]> = {}
  try {
    let session = await openSession(browser, baseline, options.prepareContext)
    const take = async (url: string, what: string, size: { width: number; height: number }, fullPage: boolean): Promise<Buffer> => {
      for (let attempt = 1; ; attempt += 1) {
        try {
          return await shoot(session, pins, url, what, size, fullPage)
        } catch (error) {
          if (!(error instanceof Unpinned)) throw error
          if (attempt >= pinTries) {
            throw new Refusal(
              `Google Fonts did not serve the static stylesheet for ${error.url} in ${pinTries} fresh contexts (it served faces from dynamically subset files, which draw differently), so ${what} was not shot`,
            )
          }
          options.log(`${what}: ${error.message}; taking it again in a fresh context`)
          await session.context.close()
          session = await openSession(browser, baseline, options.prepareContext)
        }
      }
    }
    const save = (file: string, bytes: Buffer, label: string, kind: Shot['kind']) => {
      mkdirSync(dirname(join(outDir, file)), { recursive: true })
      writeFileSync(join(outDir, file), bytes)
      shots.push({ file, hash: sha256(bytes), label, kind })
    }
    for (const [index, preset] of baseline.presets.entries()) {
      const images = new Map<Mode, Buffer>()
      for (const mode of modes) {
        const url = `${server.origin}/main.html?start=${encodeURIComponent(preset)}&still=true&apogee=${mode}`
        images.set(mode, await take(url, `the preset ${preset} (apogee=${mode})`, viewport, false))
      }
      const installed = sha256(images.get('installed') as Buffer)
      kept[preset] = modes.filter((mode) => mode === 'installed' || sha256(images.get(mode) as Buffer) !== installed)
      for (const mode of kept[preset]) {
        const file = claim(`screens/${fileName(preset)}@${mode}.png`, `${preset} (${mode})`)
        save(file, images.get(mode) as Buffer, `${preset} · ${mode}`, 'screen')
      }
      options.log(`[${index + 1}/${baseline.presets.length}] ${preset}: ${kept[preset].join(', ')}`)
    }
    for (const [board, place] of Object.entries(canvas.boards)) {
      if (board.startsWith('Screen') || aliases[board] === 'main.html') continue
      if (!existsSync(join(html, board))) throw new Refusal(`canvas.json names ${board}, which is not in html/`)
      const stem = board.replace(/\.dc\.html$/, '')
      const menus = propMenus(readFileSync(join(html, board), 'utf8'))
      const shotsOfBoard = menus.length
        ? menus.flatMap((menu) =>
            menu.options.map((value) => ({
              query: `?${encodeURIComponent(menu.name)}=${encodeURIComponent(value)}`,
              suffix: `@${fileName(menu.name)}=${fileName(value)}`,
              label: `${stem} · ${menu.name}=${value}`,
            })),
          )
        : [{ query: '', suffix: '', label: stem }]
      for (const shot of shotsOfBoard) {
        const bytes = await take(`${server.origin}/${board}${shot.query}`, `the board ${shot.label}`, { width: place.w, height: place.h }, true)
        save(claim(`boards/${fileName(stem)}${shot.suffix}.png`, shot.label), bytes, shot.label, 'board')
      }
      options.log(`board ${stem}: ${shotsOfBoard.length} image${shotsOfBoard.length === 1 ? '' : 's'}`)
    }
  } finally {
    await browser.close()
    server.stop()
  }
  return { shots, modes: kept }
}

// MARK: - The outputs

/** `<sha256>  <path>` per image, sorted by path: `shasum -a 256 -c hashes.txt` checks it in the revision's folder. */
export function hashList(shots: Shot[]): string {
  return [...shots]
    .sort((a, b) => (a.file < b.file ? -1 : a.file > b.file ? 1 : 0))
    .map((shot) => `${shot.hash}  ${shot.file}\n`)
    .join('')
}

function compare(previous: string, now: string): Comparison {
  const read = (text: string) => new Map(text.split('\n').filter(Boolean).map((line) => [line.slice(66), line.slice(0, 64)] as const))
  const before = read(previous)
  const after = read(now)
  const differ = [...new Set([...before.keys(), ...after.keys()])].filter((file) => before.get(file) !== after.get(file)).sort()
  return { same: [...after.keys()].filter((file) => before.get(file) === after.get(file)).length, differ }
}

function escape(text: string): string {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')
}

/** index.html: every image, labelled, linked to its full size, for a person to look through. */
export function contactSheet(baseline: Pick<Baseline, 'revision' | 'date' | 'presets' | 'playwright'>, shots: Shot[]): string {
  const figure = (shot: Shot) =>
    `<figure><a href="${escape(shot.file)}"><img src="${escape(shot.file)}" loading="lazy" alt="${escape(shot.label)}"></a><figcaption>${escape(shot.label)}</figcaption></figure>`
  const screens = shots.filter((shot) => shot.kind === 'screen')
  const boards = shots.filter((shot) => shot.kind === 'board')
  const extra = screens.length - baseline.presets.length
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Old prototype baseline</title>
<style>
:root { color-scheme: light dark; --bg: #f6f6f4; --fg: #1d1d1b; --muted: #5d5d58; --line: #d9d9d4; }
@media (prefers-color-scheme: dark) { :root { --bg: #161615; --fg: #ececea; --muted: #a3a39e; --line: #383836; } }
body { margin: 0; padding: 24px 16px 64px; background: var(--bg); color: var(--fg); font: 14px/1.45 system-ui, sans-serif; }
header, section { max-width: 1600px; margin: 0 auto; }
h1 { font-size: 20px; margin: 0 0 4px; }
h2 { font-size: 16px; margin: 32px 0 12px; }
p { margin: 0; color: var(--muted); }
code { font: 12px ui-monospace, Menlo, monospace; }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 360px), 1fr)); gap: 16px; }
figure { margin: 0; }
figure img { display: block; width: 100%; height: auto; max-height: 480px; object-fit: cover; object-position: top; border: 1px solid var(--line); border-radius: 4px; background: #fff; }
figcaption { margin-top: 6px; font: 12px ui-monospace, Menlo, monospace; color: var(--muted); }
</style>
</head>
<body>
<header>
<h1>The old prototype at <code>${escape(baseline.revision.slice(0, 8))}</code></h1>
<p>Revision <code>${escape(baseline.revision)}</code>, committed ${escape(baseline.date)}. ${baseline.presets.length} start presets at 1440 × 900, ${extra} more in another <code>apogee</code> mode where it draws differently, and ${boards.length} board images. Shot by Playwright ${escape(baseline.playwright)}'s Chromium; re-made by <code>npm run baseline</code> in <code>lib/design/prototype</code>.</p>
</header>
<section>
<h2>Start presets</h2>
<div class="grid">
${screens.map(figure).join('\n')}
</div>
</section>
<section>
<h2>Boards</h2>
<div class="grid">
${boards.map(figure).join('\n')}
</div>
</section>
</body>
</html>
`
}

/** baseline.json as committed: two-space JSON, each short list of strings on one line. */
function formatBaseline(baseline: Baseline): string {
  return `${JSON.stringify(baseline, null, 2).replace(/\[\n\s+("[^"\n]*")(?:,\n\s+("[^"\n]*"))?(?:,\n\s+("[^"\n]*"))?\n\s+\]/g, (_whole, ...items: unknown[]) =>
    `[${items.slice(0, 3).filter((item) => typeof item === 'string').join(', ')}]`,
  )}\n`
}

/** The images this script writes: `<outDir>/screens/*.png` and `<outDir>/boards/*.png`. */
const imageFolders = ['screens', 'boards']

function writeOutputs(outDir: string, baseline: Baseline, shots: Shot[]): Comparison | undefined {
  const listFile = join(outDir, 'hashes.txt')
  const now = hashList(shots)
  const previous = existsSync(listFile) ? compare(readFileSync(listFile, 'utf8'), now) : undefined
  writeFileSync(listFile, now)
  writeFileSync(join(outDir, 'index.html'), contactSheet(baseline, shots))
  // An image an earlier run kept and this one did not: the folder holds only what the hash list names.
  const written = new Set(shots.map((shot) => shot.file))
  for (const folder of imageFolders) {
    if (!existsSync(join(outDir, folder))) continue
    for (const name of readdirSync(join(outDir, folder))) {
      if (name.endsWith('.png') && !written.has(`${folder}/${name}`)) rmSync(join(outDir, folder, name))
    }
  }
  return previous
}

// MARK: - The two modes

/** A fresh temp folder for an archive, removed when `use` is done. */
export async function inArchive<T>(repo: string, revision: string, use: (html: string) => Promise<T>): Promise<T> {
  const folder = mkdtempSync(join(tmpdir(), 'apogee-baseline-'))
  try {
    return await use(archiveSpec(repo, revision, folder))
  } finally {
    rmSync(folder, { recursive: true, force: true })
  }
}

/**
 * `--record`: refuses while lib/design/spec shows any change, then takes the
 * newest commit touching it, runs the 30-tick check there in every mode,
 * shoots it, and writes baseline.json with this record added to its history.
 */
export async function record(options: Options & { reason: string; today: string }): Promise<Baseline> {
  const dirty = dirtySpec(options.repo)
  if (dirty.length) {
    throw new Refusal(
      `${specPath} has changes no commit holds, so no revision could be recorded with them: ${dirty.join(', ')}. Commit them, or wait for whoever is making them, and record again.`,
    )
  }
  const revision = newestSpecCommit(options.repo)
  const earlier = existsSync(options.baselineFile) ? (JSON.parse(readFileSync(options.baselineFile, 'utf8')) as Baseline) : undefined
  return inArchive(options.repo, revision, async (html) => {
    const main = readFileSync(join(html, 'main.html'), 'utf8')
    const draft: Baseline = {
      revision,
      date: git(options.repo, ['show', '-s', '--format=%cI', revision]).toString('utf8').trim(),
      tree: specTree(options.repo, revision),
      presets: readPresets(main),
      modes: {},
      thirtyTicks: Object.fromEntries(modes.map((mode) => [mode, thirtyTicks(main, mode)])) as Record<Mode, string>,
      playwright: playwrightVersion(),
      viewport: { ...viewport },
      ...fixed,
      fonts: {},
      history: [...(earlier?.history ?? []), { revision, recorded: options.today, reason: options.reason }],
    }
    for (const mode of modes) options.log(`30 ticks, apogee=${mode}: ${draft.thirtyTicks[mode]}`)
    const outDir = join(options.outRoot, revision)
    const { shots, modes: kept } = await shootAll(html, draft, { fonts: draft.fonts, recording: true }, outDir, options)
    const baseline: Baseline = { ...draft, modes: kept }
    writeOutputs(outDir, baseline, shots)
    mkdirSync(dirname(options.baselineFile), { recursive: true })
    writeFileSync(options.baselineFile, formatBaseline(baseline))
    options.log(`recorded ${revision}: ${shots.length} images in ${outDir}`)
    return baseline
  })
}

/**
 * The default: re-makes the recorded revision's images. Refuses a revision
 * the repository does not hold, a tree or preset list that is not the
 * revision's, a different Playwright, and any broken render; fails when the
 * kept modes are not the recorded ones or the images differ from the last
 * run's.
 */
export async function capture(options: Options): Promise<{ baseline: Baseline; shots: Shot[]; outDir: string; previous?: Comparison }> {
  if (!existsSync(options.baselineFile)) throw new Refusal(`${options.baselineFile} is missing: record one with npm run baseline -- --record --reason "<why>"`)
  const baseline = JSON.parse(readFileSync(options.baselineFile, 'utf8')) as Baseline
  if (!isReachable(options.repo, baseline.revision)) {
    throw new Refusal(
      `the recorded revision ${baseline.revision} is not in this repository's history, so its images cannot be re-made: fetch it (release branches merge into stable, so it stays reachable there), or re-record.`,
    )
  }
  const tree = specTree(options.repo, baseline.revision)
  if (tree !== baseline.tree) {
    throw new Refusal(`${specPath} at ${baseline.revision} is tree ${tree}, but baseline.json says ${baseline.tree}`)
  }
  const installed = playwrightVersion()
  if (installed !== baseline.playwright) {
    throw new Refusal(
      `baseline.json was shot with Playwright ${baseline.playwright}, and ${installed} is installed: its Chromium draws differently. Install ${baseline.playwright} (npm ci), or re-record with a reason.`,
    )
  }
  return inArchive(options.repo, baseline.revision, async (html) => {
    const presets = readPresets(readFileSync(join(html, 'main.html'), 'utf8'))
    if (JSON.stringify(presets) !== JSON.stringify(baseline.presets)) {
      const lacking = presets.filter((preset) => !baseline.presets.includes(preset))
      const extra = baseline.presets.filter((preset) => !presets.includes(preset))
      throw new Refusal(
        `baseline.json's preset list is not the data-props list of ${baseline.revision}'s main.html: ` +
          `the revision has ${lacking.join(', ') || 'nothing'} that baseline.json lacks, and baseline.json has ${extra.join(', ') || 'nothing'} that the revision lacks` +
          (lacking.length || extra.length ? '' : ', in another order'),
      )
    }
    const outDir = join(options.outRoot, baseline.revision)
    const { shots, modes: kept } = await shootAll(html, baseline, { fonts: { ...baseline.fonts }, recording: false }, outDir, options)
    const previous = writeOutputs(outDir, baseline, shots)
    options.log(`${shots.length} images in ${outDir}`)
    const moved = baseline.presets.filter((preset) => JSON.stringify(kept[preset]) !== JSON.stringify(baseline.modes[preset]))
    if (moved.length) {
      throw new Refusal(`the modes kept differ from baseline.json's for ${moved.map((preset) => `${preset} (now ${kept[preset]?.join(', ')})`).join('; ')}: the capture is not reproducing the record`)
    }
    if (previous) {
      if (previous.differ.length) {
        const count = previous.differ.length === 1 ? '1 image differs' : `${previous.differ.length} images differ`
        throw new Refusal(`${count} from the last run's hash list (${previous.same} match): ${previous.differ.join(', ')}`)
      }
      options.log(`the same as the last run, hash for hash: ${previous.same} images`)
    }
    return { baseline, shots, outDir, previous }
  })
}

// MARK: - Running it

/** Today in the local calendar, YYYY-MM-DD. */
function today(): string {
  const now = new Date()
  return `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`
}

if (import.meta.main) {
  const prototype = resolve(dirname(fileURLToPath(import.meta.url)), '..')
  const args = process.argv.slice(2)
  try {
    const repo = git(prototype, ['rev-parse', '--show-toplevel']).toString('utf8').trim()
    const options: Options = {
      repo,
      baselineFile: join(prototype, 'parity/baseline.json'),
      outRoot: join(prototype, '.parity/baseline'),
      log: (line) => console.log(line),
    }
    if (args.includes('--record')) {
      const reason = args[args.indexOf('--reason') + 1]
      if (!args.includes('--reason') || !reason || reason.startsWith('--')) {
        throw new Refusal('--record needs --reason "<why>": it goes into baseline.json\'s history')
      }
      await record({ ...options, reason, today: today() })
    } else {
      await capture(options)
    }
  } catch (error) {
    if (!(error instanceof Refusal)) throw error
    console.error(`npm run baseline: ${error.message}`)
    process.exitCode = 1
  }
}
