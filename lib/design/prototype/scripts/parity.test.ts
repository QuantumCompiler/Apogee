import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { chromium } from 'playwright'
import { afterEach, describe, expect, test } from 'vitest'
import { type Baseline, fileName, Refusal } from './baseline.ts'
import {
  againCheck,
  type Check,
  comparePairs,
  counterpartProblems,
  diffOutlines,
  type Difference,
  judge,
  type Judged,
  newPresets,
  outlineOf,
  outlinesList,
  pairsOf,
  parseOutline,
  readVerdicts,
  rejudge,
  repoContext,
  report,
  summaryTable,
  type Verdict,
  verdictProblems,
} from './parity.ts'

/** The prototype's root, lib/design/prototype: this file is scripts/parity.test.ts. */
const prototype = resolve(dirname(fileURLToPath(import.meta.url)), '..')

const made: string[] = []
afterEach(() => {
  for (const folder of made.splice(0)) rmSync(folder, { recursive: true, force: true })
})

/** A scratch folder, removed after the test. */
function scratch(): string {
  const folder = mkdtempSync(join(tmpdir(), 'apogee-parity-'))
  made.push(folder)
  return folder
}

/** A planted screen's outline, as Playwright's ARIA snapshot writes it. */
const oldScreen = `- complementary "Sidebar":
  - navigation "Places":
    - button "Sessions ⌘1"
    - button "Models ⌘2"
- banner:
  - heading "Models" [level=1]
  - tablist "Show":
    - tab "Backends 13" [selected]
    - tab "Stored weights 19"
    - tab "Jobs"
  - button "Download from Hugging Face…"
- 'button "local Gemma 4 12B · Q4_K_M GGUF 7.3 GB ready"':
  - img "local"
  - text: Gemma 4 12B · Q4_K_M GGUF 7.3 GB ready
- dialog "Delete Gemma?":
  - heading "Delete Gemma?" [level=2]
  - paragraph: "It frees 7.3 GB: the weights and their backend."
  - button "Cancel"
  - button "Delete"`

describe('the outline: Playwright’s ARIA snapshot read as lines', () => {
  test('roles, quoted names, attributes, inline text, a quoted key, and a property line', () => {
    const nodes = parseOutline(`- 'button "Review PR 482: retry budget 17:26"':
  - img "subscription"
  - text: "Review PR 482: retry budget 17:26"
- button "Settings (Command comma)": Settings ⌘,
- tab "Backends 13" [selected]
- heading "It’s \\"quoted\\"" [level=2]
- link "Docs":
  - /url: https://example.com/a
- paragraph: 'It''s here'
- text: plain words`)
    expect(nodes.map((node) => [node.role, node.name, node.attrs, node.text ?? null])).toEqual([
      ['button', 'Review PR 482: retry budget 17:26', [], null],
      ['button', 'Settings (Command comma)', [], 'Settings ⌘,'],
      ['tab', 'Backends 13', ['selected'], null],
      ['heading', 'It’s "quoted"', ['level=2'], null],
      ['link', 'Docs', [], null],
      ['paragraph', '', [], "It's here"],
      ['text', '', [], 'plain words'],
    ])
    expect(nodes[0]?.children.map((child) => [child.role, child.name, child.text ?? null])).toEqual([
      ['img', 'subscription', null],
      ['text', '', 'Review PR 482: retry budget 17:26'],
    ])
    expect(nodes[4]?.props).toEqual({ url: 'https://example.com/a' })
  })

  test('an empty page reads as no lines', () => {
    expect(parseOutline('')).toEqual([])
  })
})

describe('the outline differ', () => {
  const lines = (differences: Difference[]) => differences.map((difference) => difference.line)

  test('the same outline twice: no difference', () => {
    expect(diffOutlines(oldScreen, oldScreen)).toEqual([])
  })

  test('a planted missing button shows as a missing control, named', () => {
    const differences = diffOutlines(oldScreen, oldScreen.replace('  - button "Download from Hugging Face…"\n', ''))
    expect(differences).toHaveLength(1)
    expect(differences[0]).toMatchObject({ change: 'missing', kind: 'control', role: 'button', line: '- button "Download from Hugging Face…"', within: 'banner' })
  })

  test('a planted renamed tab shows as a renamed control, both names said', () => {
    const differences = diffOutlines(oldScreen, oldScreen.replace('tab "Jobs"', 'tab "Job runs"'))
    expect(differences).toHaveLength(1)
    expect(differences[0]).toMatchObject({ change: 'renamed', kind: 'control', role: 'tab', line: '~ tab "Jobs" → tab "Job runs"', within: 'banner › tablist "Show"' })
  })

  test('a tab no longer selected shows as a changed state', () => {
    const differences = diffOutlines(oldScreen, oldScreen.replace('tab "Backends 13" [selected]', 'tab "Backends 13"'))
    expect(lines(differences)).toEqual(['~ tab "Backends 13" [selected] → tab "Backends 13"'])
    expect(differences[0]?.change).toBe('state')
  })

  test('a planted lost dialog shows as one missing dialog, its lines with it', () => {
    const lost = oldScreen.slice(0, oldScreen.indexOf('- dialog'))
    const differences = diffOutlines(oldScreen, lost.trimEnd())
    expect(differences).toHaveLength(1)
    expect(differences[0]).toMatchObject({ change: 'missing', kind: 'control', role: 'dialog', line: '- dialog "Delete Gemma?"' })
    expect(differences[0]?.inner).toEqual(['heading "Delete Gemma?" [level=2]', 'paragraph: "It frees 7.3 GB: the weights and their backend."', 'button "Cancel"', 'button "Delete"'])
  })

  test('a changed fixture name inside a row shows as text, not as a control', () => {
    const differences = diffOutlines(
      oldScreen,
      oldScreen
        .replace('\'button "local Gemma 4 12B · Q4_K_M GGUF 7.3 GB ready"\'', 'button "local llama3.2-1b-Q4KM GGUF 7.3 GB ready"')
        .replace('text: Gemma 4 12B · Q4_K_M GGUF 7.3 GB ready', 'text: llama3.2-1b-Q4KM GGUF 7.3 GB ready'),
    )
    expect(differences).toHaveLength(1)
    expect(differences[0]).toMatchObject({
      change: 'renamed',
      kind: 'text',
      role: 'text',
      line: '~ text: "Gemma 4 12B · Q4_K_M GGUF 7.3 GB ready" → text: "llama3.2-1b-Q4KM GGUF 7.3 GB ready"',
    })
    expect(differences[0]?.within).toBe('button (named by its content)')
  })

  test('a long text changed in a few words shows the words that changed, with a few around them', () => {
    const words = 'The contract lists each part the app shows and how it reads the CLI, ' + 'one row a part, '.repeat(12)
    const old = `- paragraph: ${JSON.stringify(`${words}and its gaps: G1 to G24.`)}`
    const now = `- paragraph: ${JSON.stringify(`${words}and its gaps: G1 to G32, each Later.`)}`
    expect(diffOutlines(old, now).map((difference) => [difference.change, difference.kind, difference.line])).toEqual([
      ['renamed', 'text', '~ paragraph: …gaps: G1 to [-G24.-]{+G32, each Later.+}'],
    ])
  })

  test('what is left pairs with its like: a button added before three renamed ones shows as one added, not four renamed', () => {
    const old = `- banner:
  - heading "Session" [level=1]
  - button "Model: gpt-oss-20b, local. Switch model"
  - button "Thinking: auto"
  - button "Context window: 84 percent full. Show details"`
    const now = `- banner:
  - heading "Session" [level=1]
  - button "no suite"
  - button "Model: root, local. Switch model"
  - button "Thinking: default"
  - button "Context window: not reported. Show details"`
    expect(diffOutlines(old, now).map((difference) => difference.line)).toEqual([
      '+ button "no suite"',
      '~ button "Model: gpt-oss-20b, local. Switch model" → button "Model: root, local. Switch model"',
      '~ button "Thinking: auto" → button "Thinking: default"',
      '~ button "Context window: 84 percent full. Show details" → button "Context window: not reported. Show details"',
    ])
  })

  test('a button pairs with its like, even where pairing by order would pair more: Thinking with Thinking, the others gone and come', () => {
    expect(diffOutlines('- button "Model: gpt-oss-20b"\n- button "Thinking: auto"', '- button "Thinking: default"\n- button "Open the library"').map((difference) => difference.line)).toEqual([
      '- button "Model: gpt-oss-20b"',
      '~ button "Thinking: auto" → button "Thinking: default"',
      '+ button "Open the library"',
    ])
  })

  test('of two that share words, the one that shares more pairs', () => {
    expect(diffOutlines('- button "Context window: 84 percent full"', '- button "Context window: unknown"\n- button "Context 84 percent"').map((difference) => difference.line)).toEqual([
      '+ button "Context window: unknown"',
      '~ button "Context window: 84 percent full" → button "Context 84 percent"',
    ])
  })

  test('lines with no word in common do not pair: one goes, the other comes', () => {
    expect(diffOutlines('- text: "gpt-oss-20b 14:00 in 3,214"', '- text: "SOURCES 2"').map((difference) => difference.line)).toEqual(['- text: "gpt-oss-20b 14:00 in 3,214"', '+ text: "SOURCES 2"'])
  })

  test('a landmark around the same lines shows as the landmark alone', () => {
    const wrapped = oldScreen.replace('- banner:\n', '- main "Models":\n')
    expect(lines(diffOutlines(oldScreen, wrapped))).toEqual(['~ banner → main "Models"'])
    const added = `- main "Models":\n${oldScreen.replace(/^/gm, '  ')}`
    expect(lines(diffOutlines(oldScreen, added))).toEqual(['+ main "Models"'])
  })
})

/** A judged run's problems, each its own line. */
function problemsOf(checks: Check[]): string[] {
  return checks.flatMap((check) => check.problems)
}

/** A planted world: one preset and one verdict file. */
function planted(verdicts: Verdict[], newOutline = oldScreen.replace('tab "Jobs"', 'tab "Job runs"')) {
  const pairs = comparePairs([{ id: 'models@installed', preset: 'models', mode: 'installed', old: oldScreen, now: newOutline }])
  return judge(pairs, verdicts)
}

const fixed = (over: Partial<Verdict> = {}): Verdict => ({
  presets: ['models@installed'],
  match: '^~ tab "Jobs" → tab "Job runs"$',
  class: 'fix',
  item: '1h',
  reason: 'The tab is renamed, as 1h records.',
  ...over,
})

describe('the verdicts', () => {
  const context = {
    milestones: '### 2026-11-19 — `models-screens` (backlog item 1h): the Models place\n',
    fixtureExists: (name: string) => name === 'models-list-store-handmade.json',
  }

  test('a difference a verdict classes passes, with its class and reason', () => {
    const run = planted([fixed()])
    expect(problemsOf(run.checks)).toEqual([])
    expect(run.pairs[0]?.judged.map((judged: Judged) => [judged.difference.line, judged.verdict?.class])).toEqual([['~ tab "Jobs" → tab "Job runs"', 'fix']])
  })

  test('a planted difference with no verdict fails, naming the preset and the line', () => {
    const run = planted([])
    expect(problemsOf(run.checks)).toEqual(['models@installed: no verdict for ~ tab "Jobs" → tab "Job runs" (in banner › tablist "Show")'])
  })

  test('a verdict for another preset does not class it', () => {
    const run = planted([fixed({ presets: ['training@installed'] })])
    expect(problemsOf(run.checks)).toContain('models@installed: no verdict for ~ tab "Jobs" → tab "Job runs" (in banner › tablist "Show")')
  })

  test('a verdict marked defect fails it', () => {
    const run = planted([fixed({ class: 'defect', item: undefined, reason: 'The tab lost its old word.' })])
    expect(problemsOf(run.checks)).toEqual(['models@installed: a defect is open: ~ tab "Jobs" → tab "Job runs" (The tab lost its old word.)'])
  })

  test('a fix verdict that cites no item fails it', () => {
    const verdicts = [fixed({ item: undefined })]
    expect(verdictProblems(verdicts, context)).toEqual(['verdict 1 (^~ tab "Jobs" → tab "Job runs"$): a fix cites no item (its "item", as "1h")'])
  })

  test('a fix verdict citing an item MILESTONES.md has no record of fails it', () => {
    expect(verdictProblems([fixed({ item: '1z' })], context)).toEqual(['verdict 1 (^~ tab "Jobs" → tab "Job runs"$): it cites item 1z, which MILESTONES.md records no "(backlog item 1z)" for'])
  })

  test('a data verdict must name a fixture that exists', () => {
    const data = (fixture?: string): Verdict => ({ presets: ['models'], match: 'x', class: 'data', fixture, reason: 'the CLI’s list' })
    expect(verdictProblems([data()], context)).toEqual(['verdict 1 (x): a data verdict names no fixture (its "fixture")'])
    expect(verdictProblems([data('nope.json')], context)).toEqual(['verdict 1 (x): its fixture nope.json is in neither fixture root'])
    expect(verdictProblems([data('models-list-store-handmade.json')], context)).toEqual([])
  })

  test('a verdict needs a class, a reason, a pattern that compiles and a preset', () => {
    expect(verdictProblems([{ presets: [], match: '(', class: 'other' as Verdict['class'], reason: '' }], context)).toEqual([
      'verdict 1 ((): its class must be data, fix or defect',
      'verdict 1 ((): it gives no reason',
      'verdict 1 ((): it names no preset',
      'verdict 1 ((): its match is not a pattern: Invalid regular expression: /(/u: Unterminated group',
    ])
  })

  test('a verdict that classes nothing fails, so the file holds only what is found', () => {
    const run = planted([fixed(), fixed({ match: '^- button "Gone"$' })])
    expect(problemsOf(run.checks)).toEqual(['verdict 2 (^- button "Gone"$): it classes no difference'])
  })

  test('a verdict’s presets: a pair, a preset in every mode, or a pattern with *', () => {
    for (const presets of [['models@installed'], ['models'], ['model*'], ['*@installed'], ['*']]) {
      expect(problemsOf(planted([fixed({ presets })]).checks)).toEqual([])
    }
    expect(problemsOf(planted([fixed({ presets: ['models@missing'] })]).checks)).toHaveLength(2)
  })

  test('a verdict may list its differences’ lines exactly instead of a pattern', () => {
    const exact = fixed({ match: undefined, lines: ['~ tab "Jobs" → tab "Job runs"'] })
    expect(problemsOf(planted([exact]).checks)).toEqual([])
    expect(verdictProblems([exact], context)).toEqual([])
    expect(verdictProblems([fixed({ match: undefined })], context)).toEqual(['verdict 1 (no lines): it has neither a match nor lines'])
    expect(problemsOf(planted([fixed({ match: undefined, lines: ['~ tab "Jobs" → tab "Jobs!"'] })]).checks)).toHaveLength(2)
  })

  test('a verdict’s within narrows it to the lines inside a region', () => {
    expect(problemsOf(planted([fixed({ within: 'tablist "Show"' })]).checks)).toEqual([])
    expect(problemsOf(planted([fixed({ within: 'dialog' })]).checks)).toHaveLength(2)
  })

  test('a verdict whose match would class a planted lost or renamed control fails: it must list what it classes, or name their words', () => {
    for (const match of ['^[~+-] (text|button|heading|tab|dialog|alertdialog)\\b.*$', '^[~+-] .*$', '^- (alertdialog|dialog) "[^"]*"$', '^~ heading "[^"]*" \\[level=2\\] → heading "[^"]*" \\[level=2\\]$']) {
      const problems = verdictProblems([fixed({ match })], context)
      expect(problems).toHaveLength(1)
      expect(problems[0]).toMatch(/^verdict 1 \(.*\): its match classes a planted line no record names \((- button|\+ button|- tab|~ heading|- dialog|- alertdialog) "⁂ ⁑ 0713 ⁂"/)
    }
    expect(verdictProblems([fixed({ match: '^~ tab "Jobs" → tab "Job runs"$' }), fixed({ match: '^- button "(cloud|local) [^"]*"$' })], context)).toEqual([])
  })

  test('readVerdicts reads the committed file: every verdict valid against the real MILESTONES.md and fixture roots', () => {
    const file = readVerdicts(join(prototype, 'parity/verdicts.json'))
    expect(file.verdicts.length).toBeGreaterThan(0)
    expect(verdictProblems(file.verdicts, repoContext(prototype))).toEqual([])
    expect(file.verdicts.filter((verdict) => verdict.class === 'defect')).toEqual([])
  })
})

describe('the committed verdicts.json against planted losses (design 1s)', () => {
  /** One pair whose only difference is a planted line, judged by the committed file. */
  const plantedLoss = (id: string, role: string, line: string, within: string) => {
    const [preset, mode] = id.split('@') as [string, string]
    const run = judge([{ id, preset, mode, differences: [{ change: 'missing', kind: 'control', role, line, within, inner: [] }] }], readVerdicts(join(prototype, 'parity/verdicts.json')).verdicts)
    return run.pairs[0]?.judged[0]?.verdict
  }

  test('a lost New task… on tasks and a lost Jobs tab on models come back unjudged', () => {
    expect(plantedLoss('tasks@installed', 'button', '- button "New task…"', 'banner')).toBeUndefined()
    expect(plantedLoss('models@installed', 'tab', '- tab "Jobs"', 'banner › tablist "Show"')).toBeUndefined()
  })
})

describe('the presets', () => {
  test('a preset in baseline.json with no counterpart in the new app fails, naming the preset', () => {
    expect(counterpartProblems(['chat', 'models', 'planted:gone'], ['chat', 'models'])).toEqual([
      'the preset planted:gone in baseline.json has no counterpart in the new app: no frame in gallery/Models/Boards.ts starts at it',
    ])
    expect(counterpartProblems(['chat'], ['chat', 'settings:roles'])).toEqual([])
  })

  test('the new app’s presets are its board table’s starts, and every one of the baseline’s has one', () => {
    const baseline = JSON.parse(readFileSync(join(prototype, 'parity/baseline.json'), 'utf8')) as { presets: string[] }
    const presets = newPresets()
    expect(counterpartProblems(baseline.presets, presets)).toEqual([])
    expect(presets.filter((preset) => !baseline.presets.includes(preset))).toEqual(['training:regimes', 'knowledge:ingested-with-a-note', 'knowledge:captured-with-a-note', 'graph:built-with-a-note', 'settings:roles', 'settings:web', 'settings:memory', 'settings:keyboard'])
  })

  test('the pairs: each preset in each mode 1c kept, the new presets at installed, and the 10 rebuilt boards at each prop value 1c shot', () => {
    const baseline = JSON.parse(readFileSync(join(prototype, 'parity/baseline.json'), 'utf8')) as Pick<Baseline, 'presets' | 'modes'>
    const pairs = pairsOf(baseline, newPresets())
    const screens = pairs.filter((pair) => pair.kind === 'screen')
    // 1c's 97, then the presets the new app adds: 1j's and 1i's five, and GUI M22's three runs that left a note.
    expect(screens).toHaveLength(97 + 8)
    expect(pairs.find((pair) => pair.id === 'library@config-typo')).toMatchObject({ preset: 'library', mode: 'config-typo', old: { image: 'screens/library@config-typo.png' } })
    expect(pairs.find((pair) => pair.id === 'training:regimes@installed')).toMatchObject({ old: { start: 'training', click: 'Regimes' } })
    expect(pairs.find((pair) => pair.id === 'settings:roles@installed')?.old.image).toBeUndefined()
    const boards = pairs.filter((pair) => pair.kind === 'board').map((pair) => pair.id)
    expect(boards).toEqual([
      'BuildContract',
      'BuildLayout',
      'BuildProcesses',
      'ColorTokens@chrome=dark',
      'ColorTokens@chrome=light',
      'TypeSpace@theme=dark',
      'TypeSpace@theme=light',
      'ComponentsModel@theme=dark',
      'ComponentsModel@theme=light',
      'ComponentsModel@theme=contrast',
      'ComponentsTurn@theme=dark',
      'ComponentsTurn@theme=light',
      'ComponentsTurn@theme=contrast',
      'ComponentsPrompts@theme=dark',
      'ComponentsPrompts@theme=light',
      'ComponentsPrompts@theme=contrast',
      'ComponentsMedia@theme=dark',
      'ComponentsMedia@theme=light',
      'ComponentsMedia@theme=contrast',
      'Logo@theme=dark',
      'Logo@theme=light',
    ])
    expect(pairs.find((pair) => pair.id === 'ColorTokens@chrome=light')).toMatchObject({ old: { board: 'ColorTokens.dc.html', query: '?chrome=light', image: 'boards/ColorTokens@chrome=light.png' }, now: { board: 'ColorTokens', theme: 'light' } })
    expect(pairs.find((pair) => pair.id === 'BuildContract')).toMatchObject({ now: { board: 'BuildContract', theme: 'dark' } })
  })
})

describe('two runs', () => {
  test('the same build gives the same outlines; a run that differs is named', () => {
    const one = outlinesList('sha256-a', [{ file: 'outlines/chat@installed.new.yml', text: 'x' }])
    const two = outlinesList('sha256-a', [{ file: 'outlines/chat@installed.new.yml', text: 'y' }])
    const three = outlinesList('sha256-b', [{ file: 'outlines/chat@installed.new.yml', text: 'y' }])
    const checks = (previous: string | undefined, now: string) => problemsOf([againCheck(previous, now)])
    expect(checks(undefined, one)).toEqual([])
    expect(checks(one, one)).toEqual([])
    expect(checks(one, two)).toEqual(['the same build (sha256-a) gave different outlines from the last run: outlines/chat@installed.new.yml'])
    expect(checks(one, three)).toEqual([])
  })

  test('a run with nothing to compare says so, rather than passing: no earlier run, or another build’s', () => {
    const one = outlinesList('sha256-a', [{ file: 'outlines/chat@installed.new.yml', text: 'x' }])
    const two = outlinesList('sha256-a', [{ file: 'outlines/chat@installed.new.yml', text: 'y' }])
    const three = outlinesList('sha256-b', [{ file: 'outlines/chat@installed.new.yml', text: 'y' }])
    expect(againCheck(undefined, one).skipped).toMatch(/^not compared: no earlier run/)
    expect(againCheck(one, three).skipped).toMatch(/^not compared: the last run read the build sha256-a/)
    expect(againCheck(one, one).skipped).toBeUndefined()
    expect(againCheck(one, two).skipped).toBeUndefined()
  })
})

describe('the report', () => {
  test('each pair side by side with its differences, each classed with its reason, and the summary table', () => {
    const run = planted([fixed()])
    const html = report(run, { revision: 'b375fac9de179d3f3b6cf44868c72cd54a1a8d36', built: 'sha256-a', images: { 'models@installed': { old: 'old/screens/models@installed.png', now: 'new/screens/models@installed.png' } } })
    expect(html).toContain('<img src="old/screens/models@installed.png"')
    expect(html).toContain('<img src="new/screens/models@installed.png"')
    expect(html).toContain('~ tab &quot;Jobs&quot; → tab &quot;Job runs&quot;')
    expect(html).toContain('The tab is renamed, as 1h records.')
    expect(summaryTable(run)).toBe(
      ['| Pair | Differences | data | fix | Unjudged |', '|---|---|---|---|---|', '| `models@installed` | 1 | 0 | 1 (1h) | 0 |', '| **All 1** | 1 | 0 | 1 | 0 |', ''].join('\n'),
    )
  })

  test('a check that did not run says why, not yes, and the page says only the checks that ran pass', () => {
    const run = planted([fixed()])
    run.checks.push({ name: 'the same build reads the same outlines twice', problems: [], skipped: 'not compared: no earlier run; run npm run parity again on this build' })
    const html = report(run, { revision: 'b375fac9de179d3f3b6cf44868c72cd54a1a8d36', built: 'sha256-a', images: {} })
    expect(html).toContain('<li class="skip">the same build reads the same outlines twice: not compared: no earlier run; run npm run parity again on this build</li>')
    expect(html).not.toContain('the same build reads the same outlines twice: yes')
    expect(html).toContain('Every check that ran passes.')
    expect(html).not.toContain('Every check passes.')
    expect(report(planted([fixed()]), { revision: 'b375fac9de179d3f3b6cf44868c72cd54a1a8d36', built: 'sha256-a', images: {} })).toContain('Every check passes.')
  })
})

describe('judging again (--rejudge)', () => {
  /** A planted prototype: the real baseline.json, no verdicts, a built file, MILESTONES.md, and a last run whose outlines are the same on both sides. */
  function plantedPrototype(checks?: { built: string; before: Check[]; after: Check[] }): string {
    const root = scratch()
    const at = join(root, 'lib/design/prototype')
    mkdirSync(join(at, 'parity'), { recursive: true })
    mkdirSync(join(at, '.parity/report/outlines'), { recursive: true })
    mkdirSync(join(root, 'lib/documentation/assistant/design'), { recursive: true })
    const baseline = readFileSync(join(prototype, 'parity/baseline.json'), 'utf8')
    writeFileSync(join(at, 'parity/baseline.json'), baseline)
    writeFileSync(join(at, 'parity/verdicts.json'), JSON.stringify({ verdicts: [] }))
    writeFileSync(join(root, 'lib/design/main.html'), '<!doctype html><meta name="apogee-design-sources" content="sha256-planted">')
    writeFileSync(join(root, 'lib/documentation/assistant/design/MILESTONES.md'), '# Milestones\n')
    const files: Array<{ file: string; text: string }> = []
    for (const pair of pairsOf(JSON.parse(baseline) as Baseline, newPresets())) {
      for (const side of ['old', 'new']) {
        const file = `outlines/${fileName(pair.id)}.${side}.yml`
        writeFileSync(join(at, '.parity/report', file), '- heading "Planted" [level=1]')
        files.push({ file, text: '- heading "Planted" [level=1]' })
      }
    }
    writeFileSync(join(at, '.parity/report/outlines.txt'), outlinesList('sha256-planted', files))
    if (checks) writeFileSync(join(at, '.parity/report/checks.json'), JSON.stringify(checks))
    return at
  }
  const options = (at: string) => ({ repo: resolve(at, '../../..'), prototype: at, log: () => undefined })
  const failedCapture: Check = { name: "every old frame is 1c's, hash for hash", problems: ['chat@installed: the old frame was not 1c’s (planted)'] }

  test('the last run’s capture checks stand as that run made them: a failed one still fails, and the report shows it', () => {
    const at = plantedPrototype({ built: 'sha256-planted', before: [failedCapture], after: [{ name: 'the same build reads the same outlines twice', problems: [] }] })
    const run = rejudge(options(at))
    expect(run.checks).toContainEqual(failedCapture)
    expect(run.checks.map((check) => check.name)).toContain('the same build reads the same outlines twice')
    const html = readFileSync(join(at, '.parity/report/index.html'), 'utf8')
    expect(html).toContain('chat@installed: the old frame was not 1c’s (planted)')
    expect(html).not.toContain('Every check passes.')
  })

  test('a last run that left no checks, or another build’s, is refused', () => {
    expect(() => rejudge(options(plantedPrototype()))).toThrow(Refusal)
    expect(() => rejudge(options(plantedPrototype()))).toThrow(/left no checks/)
    expect(() => rejudge(options(plantedPrototype({ built: 'sha256-other', before: [], after: [] })))).toThrow(/sha256-other/)
  })
})

describe('reading a page’s outline', () => {
  test('the outline after a click: a pressed-state button, as training:regimes’ old side is reached', async () => {
    const folder = scratch()
    const file = join(folder, 'page.html')
    writeFileSync(
      file,
      `<!doctype html><html lang="en"><body><h1>Training</h1><button aria-pressed="false" onclick="this.setAttribute('aria-pressed','true');document.querySelector('p').textContent='Regimes shown'">Regimes</button><p>Runs shown</p></body></html>`,
    )
    const browser = await chromium.launch()
    try {
      const page = await browser.newPage()
      await page.goto(pathToFileURL(file).href)
      const before = await outlineOf(page)
      await page.getByRole('button', { name: 'Regimes', exact: true }).click()
      const after = await outlineOf(page)
      expect(diffOutlines(before, after).map((difference) => difference.line)).toEqual([
        '~ button "Regimes" → button "Regimes" [pressed]',
        '~ paragraph: "Runs shown" → paragraph: "Regimes shown"',
      ])
    } finally {
      await browser.close()
    }
  })
})
