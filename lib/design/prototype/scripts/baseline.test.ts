import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import type { BrowserContext } from 'playwright'
import { afterEach, describe, expect, test } from 'vitest'
import {
  type Baseline,
  capture,
  propMenus,
  readPresets,
  record,
  Refusal,
  thirtyTicks,
} from './baseline.ts'

/** The prototype's root, lib/design/prototype: this file is scripts/baseline.test.ts. */
const prototype = resolve(dirname(fileURLToPath(import.meta.url)), '..')

/** git, read-only, in the real repository (never an optional lock, so the shared index is left alone). */
function realGit(...args: string[]): string {
  return execFileSync('git', ['--no-optional-locks', '-C', prototype, ...args], { encoding: 'utf8', maxBuffer: 1 << 30 })
}

/**
 * The real support.js, from the newest commit that wrote it, so the planted
 * boards render with the runtime the frozen prototype uses -- before and after
 * 1t deletes lib/design/spec.
 */
function realSupport(): string {
  const commit = realGit('log', '--diff-filter=AM', '-1', '--format=%H', '--', 'lib/design/spec/html/support.js').trim()
  return realGit('show', `${commit}:lib/design/spec/html/support.js`)
}

/** A Design Component main.html in miniature: one heading, a note when Apogee is missing, and `broken` throwing. */
function miniatureMain(presets: string[], head = ''): string {
  const props = JSON.stringify({
    start: { editor: 'enum', options: presets, default: presets[0] },
    still: { editor: 'boolean', default: false },
    apogee: { editor: 'enum', options: ['installed', 'missing', 'config-typo'], default: 'installed' },
    $preview: { width: 1440, height: 900 },
  }).replace(/&/g, '&amp;').replace(/'/g, '&#39;')
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Planted prototype</title>
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>${head}<style>body{margin:0;font:16px sans-serif}</style></helmet>
<main style="padding:24px"><h1>{{title}}</h1><sc-if value="{{gone}}"><p>Apogee is not installed.</p></sc-if></main>
</x-dc>
<script type="text/x-dc" data-dc-script data-props='${props}'>
class Component extends DCLogic {
  constructor(props) { super(props); this.state = { clock: 0 }; }
  tick() { this.setState({ clock: this.state.clock + 1 }); }
  renderVals() {
    if (this.props.start === 'broken') throw new Error('planted failure');
    return { title: 'Start ' + this.props.start, gone: this.props.apogee === 'missing' };
  }
}
</script>
</body>
</html>
`
}

/** A board in miniature with one prop menu, `theme`. */
const miniatureBoard = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<section style="height:1200px;padding:24px;font:16px sans-serif">Tokens in {{theme}}</section>
</x-dc>
<script type="text/x-dc" data-dc-script data-props='{"theme":{"editor":"enum","options":["dark","light"],"default":"dark"},"$preview":{"width":1440,"height":600}}'>
class Component extends DCLogic {
  renderVals() { return { theme: this.props.theme }; }
}
</script>
</body>
</html>
`

const miniatureCanvas = JSON.stringify({
  boards: {
    'App.dc.html': { w: 1440, h: 900, page: 'build' },
    'ScreenHome.dc.html': { w: 1440, h: 900, page: 'build' },
    'Tokens.dc.html': { w: 1440, h: 600, page: 'foundations' },
  },
})

const made: string[] = []
afterEach(() => {
  for (const folder of made.splice(0)) rmSync(folder, { recursive: true, force: true })
})

/** A scratch folder, removed after the test. */
function scratch(): string {
  const folder = mkdtempSync(join(tmpdir(), 'apogee-baseline-'))
  made.push(folder)
  return folder
}

/**
 * git in a planted repository, with no hook, signing or global identity in play.
 * Every GIT_* variable is dropped: an inherited GIT_DIR (a hook, or a run in a scratch
 * copy) would otherwise point `-C <temp>` at the caller's repository and commit there.
 */
function plantedGit(repo: string, ...args: string[]): string {
  const env = Object.fromEntries(Object.entries(process.env).filter(([name]) => !name.startsWith('GIT_')))
  return execFileSync(
    'git',
    ['-c', 'core.hooksPath=/dev/null', '-c', 'commit.gpgsign=false', '-c', 'user.name=Planted', '-c', 'user.email=planted@example.invalid', '-C', repo, ...args],
    { encoding: 'utf8', env },
  )
}

/** A temp git repository holding a planted lib/design/spec, committed once. */
function plantedRepo(main: string, extra: Record<string, string> = {}): string {
  const repo = scratch()
  plantedGit(repo, 'init', '-q', '-b', 'main')
  const files: Record<string, string> = {
    'lib/design/spec/html/main.html': main,
    'lib/design/spec/html/support.js': realSupport(),
    'lib/design/spec/html/ScreenHome.dc.html': '<!doctype html><title>Screen board</title>\n',
    'lib/design/spec/html/Tokens.dc.html': miniatureBoard,
    'lib/design/spec/json/canvas.json': miniatureCanvas,
    'README.md': '# planted\n',
    ...extra,
  }
  for (const [path, text] of Object.entries(files)) {
    mkdirSync(dirname(join(repo, path)), { recursive: true })
    writeFileSync(join(repo, path), text)
  }
  plantedGit(repo, 'add', '-A')
  plantedGit(repo, 'commit', '-q', '-m', 'planted spec')
  return repo
}

/** Where a planted run keeps baseline.json and its images. */
function places(): { baselineFile: string; outRoot: string } {
  const folder = scratch()
  return { baselineFile: join(folder, 'parity/baseline.json'), outRoot: join(folder, '.parity/baseline') }
}

const quiet = () => {}

describe('reading the prototype', () => {
  test("the preset reader decodes &amp; and &#39; in data-props, once", () => {
    const main = `<x-dc></x-dc>\n<script type="text/x-dc" data-dc-script data-props='{"start":{"editor":"enum","options":["chat","q&amp;a","it&#39;s","literal&amp;#39;"],"default":"chat"},"$preview":{"width":1440,"height":900}}'>\nclass Component extends DCLogic {}\n</script>\n`
    expect(readPresets(main)).toEqual(['chat', 'q&a', "it's", 'literal&#39;'])
  })

  test('a file with no start menu is refused, never read as no presets', () => {
    expect(() => readPresets(miniatureBoard)).toThrow(Refusal)
  })

  test("a board's prop menus are its enum props, never $preview or a boolean", () => {
    expect(propMenus(miniatureBoard)).toEqual([{ name: 'theme', options: ['dark', 'light'] }])
    expect(propMenus(miniatureMain(['home']))).toEqual([
      { name: 'start', options: ['home'] },
      { name: 'apogee', options: ['installed', 'missing', 'config-typo'] },
    ])
  })

  test("the 30-tick check runs each start screen 30 ticks and says what threw, as spec/CLAUDE.md's does", () => {
    expect(thirtyTicks(miniatureMain(['home', 'away']), 'installed')).toBe('2 start screens, no problems')
    expect(thirtyTicks(miniatureMain(['home', 'broken']), 'missing')).toBe('2 start screens, broken: planted failure')
  })
})

describe('--record', () => {
  test('refuses while lib/design/spec has changes no commit holds, naming the files', async () => {
    const repo = plantedRepo(miniatureMain(['home']))
    writeFileSync(join(repo, 'lib/design/spec/html/main.html'), miniatureMain(['home', 'away']))
    writeFileSync(join(repo, 'lib/design/spec/html/ScreenAway.dc.html'), '<!doctype html>\n')
    writeFileSync(join(repo, 'README.md'), '# changed outside the spec\n')
    const { baselineFile, outRoot } = places()
    const refused = record({ repo, baselineFile, outRoot, reason: 'planted', today: '2026-11-18', log: quiet })
    await expect(refused).rejects.toThrow(Refusal)
    await expect(refused).rejects.toThrow('lib/design/spec/html/main.html')
    await expect(refused).rejects.toThrow('lib/design/spec/html/ScreenAway.dc.html')
    await expect(refused).rejects.not.toThrow('README.md')
    expect(existsSync(baselineFile)).toBe(false)
  })
})

describe('capture refuses before it shoots', () => {
  /** A baseline.json for the planted repository's commit, as --record would write it. */
  function plantedBaseline(repo: string, presets: string[]): Baseline {
    const revision = plantedGit(repo, 'rev-parse', 'HEAD').trim()
    return {
      revision,
      date: '2026-11-18T00:00:00Z',
      tree: plantedGit(repo, 'rev-parse', `${revision}:lib/design/spec`).trim(),
      presets,
      modes: Object.fromEntries(presets.map((preset) => [preset, ['installed']])),
      thirtyTicks: { installed: '', missing: '', 'config-typo': '' },
      playwright: '1.64.1',
      viewport: { width: 1440, height: 900 },
      clock: '2026-11-14T09:41:00.000Z',
      timezone: 'UTC',
      locale: 'en-US',
      seed: 1,
      fonts: {},
      history: [{ revision, recorded: '2026-11-18', reason: 'planted' }],
    }
  }

  function writeBaseline(file: string, baseline: Baseline): void {
    mkdirSync(dirname(file), { recursive: true })
    writeFileSync(file, JSON.stringify(baseline, null, 2))
  }

  test('a revision this repository does not hold is refused and named', async () => {
    const repo = plantedRepo(miniatureMain(['home']))
    const { baselineFile, outRoot } = places()
    const unknown = '0123456789abcdef0123456789abcdef01234567'
    writeBaseline(baselineFile, { ...plantedBaseline(repo, ['home']), revision: unknown })
    const refused = capture({ repo, baselineFile, outRoot, log: quiet })
    await expect(refused).rejects.toThrow(Refusal)
    await expect(refused).rejects.toThrow(unknown)
    expect(existsSync(outRoot)).toBe(false)
  })

  test("a preset list that is not the revision's data-props list fails, naming the difference", async () => {
    const repo = plantedRepo(miniatureMain(['home', 'away']))
    const { baselineFile, outRoot } = places()
    writeBaseline(baselineFile, plantedBaseline(repo, ['home', 'gone']))
    const refused = capture({ repo, baselineFile, outRoot, log: quiet })
    await expect(refused).rejects.toThrow(Refusal)
    await expect(refused).rejects.toThrow(/away[\s\S]*gone/)
    expect(existsSync(outRoot)).toBe(false)
  })
})

describe('capture, in Chromium', () => {
  test("a preset that draws support.js's error box fails the capture, naming the preset", async () => {
    const repo = plantedRepo(miniatureMain(['home', 'broken']))
    const { baselineFile, outRoot } = places()
    const refused = record({ repo, baselineFile, outRoot, reason: 'planted', today: '2026-11-18', log: quiet })
    await expect(refused).rejects.toThrow(Refusal)
    await expect(refused).rejects.toThrow(/broken[\s\S]*renderVals\(\) failed: planted failure/)
    expect(existsSync(baselineFile)).toBe(false)
  }, 60_000)

  test('a Google Fonts link that fails refuses the run, never a frame in fallback fonts', async () => {
    const link = '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Planted+Sans&amp;display=swap">'
    const repo = plantedRepo(miniatureMain(['home'], link))
    const { baselineFile, outRoot } = places()
    const refused = record({
      repo,
      baselineFile,
      outRoot,
      reason: 'planted',
      today: '2026-11-18',
      log: quiet,
      // No network in a test: the fonts request fails as an offline run's would.
      prepareContext: (context) => context.route('https://fonts.googleapis.com/**', (route) => route.abort('internetdisconnected')),
    })
    await expect(refused).rejects.toThrow(Refusal)
    await expect(refused).rejects.toThrow('the fonts did not load for the preset home')
    await expect(refused).rejects.toThrow('https://fonts.googleapis.com/css2?family=Planted+Sans')
    expect(existsSync(baselineFile)).toBe(false)
  }, 60_000)

  /** The board's Google Fonts link, and stylesheets for it: a face nothing draws, so no font file is ever fetched. */
  const fontsLink = '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Planted+Sans&amp;display=swap">'
  const fontsUrl = 'https://fonts.googleapis.com/css2?family=Planted+Sans&display=swap'
  const staticCss = (version: number) =>
    `@font-face { font-family: 'Planted Unused'; src: url(https://fonts.gstatic.com/s/plantedsans/v${version}/planted.woff2) format('woff2'); }\n`
  const subsetCss = "@font-face { font-family: 'Planted Unused'; src: url(https://fonts.gstatic.com/l/font?kit=planted&skey=1&v=v1) format('woff2'); }\n"

  test('a dynamically subset stylesheet is fetched again in a fresh context, and the static one is pinned by its hash', async () => {
    const repo = plantedRepo(miniatureMain(['home'], fontsLink))
    const { baselineFile, outRoot } = places()
    let requests = 0
    const recorded = await record({
      repo,
      baselineFile,
      outRoot,
      reason: 'planted',
      today: '2026-11-18',
      log: quiet,
      // Google Fonts' two answers, without the network: the first a subset, then the static one.
      prepareContext: (context) =>
        context.route('https://fonts.googleapis.com/**', (route) => {
          requests += 1
          return route.fulfill({ status: 200, contentType: 'text/css; charset=utf-8', body: requests === 1 ? subsetCss : staticCss(1) })
        }),
    })
    expect(requests).toBeGreaterThan(1)
    expect(recorded.fonts).toEqual({ [fontsUrl]: createHash('sha256').update(staticCss(1)).digest('hex') })
  }, 60_000)

  test('a stylesheet that changed since the record refuses the capture, naming it', async () => {
    const repo = plantedRepo(miniatureMain(['home'], fontsLink))
    const { baselineFile, outRoot } = places()
    let served = staticCss(1)
    const prepareContext = (context: BrowserContext) =>
      context.route('https://fonts.googleapis.com/**', (route) => route.fulfill({ status: 200, contentType: 'text/css; charset=utf-8', body: served }))
    await record({ repo, baselineFile, outRoot, reason: 'planted', today: '2026-11-18', log: quiet, prepareContext })
    served = staticCss(2)
    const refused = capture({ repo, baselineFile, outRoot, log: quiet, prepareContext })
    await expect(refused).rejects.toThrow(Refusal)
    await expect(refused).rejects.toThrow(`Google Fonts now serves a different stylesheet for ${fontsUrl}`)
  }, 60_000)

  test('record, then capture: every preset, the modes that differ, each board per menu value, the same hashes twice', async () => {
    const repo = plantedRepo(miniatureMain(['home', 'away']))
    const head = plantedGit(repo, 'rev-parse', 'HEAD').trim()
    // A later commit that leaves the spec alone: the revision is the newest one touching it.
    writeFileSync(join(repo, 'README.md'), '# planted, again\n')
    plantedGit(repo, 'commit', '-q', '-am', 'outside the spec')
    const { baselineFile, outRoot } = places()

    const recorded = await record({ repo, baselineFile, outRoot, reason: 'planted first record', today: '2026-11-18', log: quiet })
    expect(recorded.revision).toBe(head)
    expect(recorded.tree).toBe(plantedGit(repo, 'rev-parse', `${head}:lib/design/spec`).trim())
    expect(recorded.presets).toEqual(['home', 'away'])
    // `missing` draws the note, so it is kept; `config-typo` draws what `installed` does, so it is not.
    expect(recorded.modes).toEqual({ home: ['installed', 'missing'], away: ['installed', 'missing'] })
    expect(recorded.thirtyTicks).toEqual({
      installed: '2 start screens, no problems',
      missing: '2 start screens, no problems',
      'config-typo': '2 start screens, no problems',
    })
    expect(recorded.viewport).toEqual({ width: 1440, height: 900 })
    expect(recorded.playwright).toBe('1.64.1')
    expect(recorded.history).toEqual([{ revision: head, recorded: '2026-11-18', reason: 'planted first record' }])
    expect(JSON.parse(readFileSync(baselineFile, 'utf8'))).toEqual(recorded)

    const out = join(outRoot, head)
    const first = readFileSync(join(out, 'hashes.txt'), 'utf8')
    expect(first.trim().split('\n').map((line) => line.split('  ')[1])).toEqual([
      'boards/Tokens@theme=dark.png',
      'boards/Tokens@theme=light.png',
      'screens/away@installed.png',
      'screens/away@missing.png',
      'screens/home@installed.png',
      'screens/home@missing.png',
    ])
    expect(readFileSync(join(out, 'index.html'), 'utf8')).toContain('screens/home@missing.png')

    // The spec deleted from the working tree, as 1t will: capture re-makes the same images from history.
    rmSync(join(repo, 'lib/design/spec'), { recursive: true })
    // An image an earlier run kept and this one does not: the folder ends holding only what the hash list names.
    writeFileSync(join(out, 'screens/home@config-typo.png'), 'stale')
    writeFileSync(join(out, 'notes.txt'), 'not the script’s')
    const again = await capture({ repo, baselineFile, outRoot, log: quiet })
    expect(again.previous).toEqual({ same: 6, differ: [] })
    expect(readFileSync(join(out, 'hashes.txt'), 'utf8')).toBe(first)
    expect(existsSync(join(out, 'screens/home@config-typo.png'))).toBe(false)
    expect(existsSync(join(out, 'notes.txt'))).toBe(true)

    // A hash list from a run that drew one image differently: the next capture names it and fails.
    writeFileSync(join(out, 'hashes.txt'), first.replace(/^[0-9a-f]{64}(?= {2}screens\/home@installed\.png)/m, '0'.repeat(64)))
    const differs = capture({ repo, baselineFile, outRoot, log: quiet })
    await expect(differs).rejects.toThrow(Refusal)
    await expect(differs).rejects.toThrow("1 image differs from the last run's hash list (5 match): screens/home@installed.png")
  }, 120_000)
})

describe('the committed baseline', () => {
  test("parity/baseline.json's preset list is its revision's data-props list, and its tree is the revision's", () => {
    const baseline = JSON.parse(readFileSync(join(prototype, 'parity/baseline.json'), 'utf8')) as Baseline
    const main = realGit('show', `${baseline.revision}:lib/design/spec/html/main.html`)
    expect(baseline.presets).toEqual(readPresets(main))
    expect(baseline.tree).toBe(realGit('rev-parse', `${baseline.revision}:lib/design/spec`).trim())
    expect(Object.keys(baseline.modes)).toEqual(baseline.presets)
    expect(baseline.history.at(-1)?.revision).toBe(baseline.revision)
  })
})
