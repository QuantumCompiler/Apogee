/**
 * The parity proof (design 1s): the React app against the old prototype,
 * preset by preset, before 1t deletes the old files.
 *
 *   npm run parity        # capture both sides, diff their outlines, judge each difference, write the report
 *
 * The old side is 1c's baseline: its revision archived and served exactly as
 * `npm run baseline` serves it (`scripts/baseline.ts`, whose frame this
 * reuses -- the fixed clock, time zone, locale and seed, each image in a page
 * of its own on its second load, one raster thread, Google Fonts held to the
 * stylesheets baseline.json pins). Every old image this run shoots must hash
 * to 1c's hash list, so each outline read beside it is the baseline's frame.
 * The images are 1c's, re-made by `npm run baseline` first when the folder no
 * longer matches its hash list. The new side is the built file opened from
 * disk, `lib/design/main.html?start=<preset>&still=true&apogee=<mode>`, and
 * the gallery's `?gallery&board=<Board>&theme=<theme>` for the 10 rebuilt
 * boards, in the same frame.
 *
 * Each side is read as an outline, Playwright's ARIA snapshot (landmarks,
 * headings, tabs, buttons, fields, menus, dialogs, their labels and the text
 * around them), the two outlines are diffed, and every difference must be
 * classed by `parity/verdicts.json`: `data` (the CLI's recorded data in place
 * of the old's invented data, naming its fixture) or `fix` (a change a
 * shipped item records, citing it). A difference with no verdict, or a
 * `defect`, fails the run. The pictures are for the eye; the outlines are the
 * measure. The report, `.parity/report/index.html`, puts each pair side by
 * side with its differences, beside its images, and is never committed.
 */
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { basename, dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { type Browser, chromium, type Page } from 'playwright'
import { boards } from '../source/presentation/gallery/Models/Boards.ts'
import {
  type Baseline,
  browserArgs,
  capture as captureBaseline,
  checkFonts,
  fileName,
  inArchive,
  isReachable,
  load,
  openSession,
  pinTries,
  type Pins,
  playwrightVersion,
  prepareWindow,
  Refusal,
  serve,
  type Session,
  sha256,
  specTree,
  Unpinned,
  viewport,
} from './baseline.ts'

// MARK: - The outline

/** One node of Playwright's ARIA snapshot: its role, name, states, properties, the text it holds inline, and its children. */
export interface OutlineNode {
  role: string
  name: string
  /** `selected`, `expanded`, `level=2`, `pressed`, `checked`, `disabled`, as the snapshot writes them. */
  attrs: string[]
  /** `/url: …`, `/placeholder: …`: a property line under the node. */
  props: Record<string, string>
  /** Text written on the node's own line (`- paragraph: …`, `- text: …`, `- button "x": …`). */
  text?: string
  children: OutlineNode[]
}

/** A YAML scalar as Playwright writes one: single-quoted (`''` for a quote), double-quoted (JSON's escapes), or plain. */
function scalar(raw: string): string {
  const text = raw.trim()
  if (text.startsWith("'") && text.endsWith("'") && text.length >= 2) return text.slice(1, -1).replace(/''/g, "'")
  if (text.startsWith('"') && text.endsWith('"') && text.length >= 2) return JSON.parse(text) as string
  return text
}

/** Where a double-quoted string starting at `start` ends: the index of its closing quote. */
function closingQuote(text: string, start: number): number {
  for (let at = start + 1; at < text.length; at += 1) {
    if (text[at] === '\\') at += 1
    else if (text[at] === '"') return at
  }
  return -1
}

/** A line's key and value: the key quoted whole when it holds `: `, or plain up to the first `:` outside its quoted name. */
function keyAndValue(content: string): { key: string; value?: string; opens: boolean } {
  let keyEnd: number
  let key: string
  if (content.startsWith("'")) {
    let at = 1
    while (at < content.length) {
      if (content[at] === "'" && content[at + 1] === "'") at += 2
      else if (content[at] === "'") break
      else at += 1
    }
    key = content.slice(1, at).replace(/''/g, "'")
    keyEnd = at + 1
  } else {
    let at = 0
    keyEnd = content.length
    while (at < content.length) {
      if (content[at] === '"') {
        const close = closingQuote(content, at)
        at = close < 0 ? content.length : close + 1
      } else if (content[at] === ':' && (at + 1 === content.length || content[at + 1] === ' ')) {
        keyEnd = at
        break
      } else at += 1
    }
    key = content.slice(0, keyEnd)
  }
  const rest = content.slice(keyEnd)
  if (!rest.startsWith(':')) return { key, opens: false }
  const value = rest.slice(1)
  return value.trim() === '' ? { key, opens: true } : { key, value: scalar(value), opens: false }
}

/** `role "name" [attr] [attr=value]`. */
function readKey(key: string): { role: string; name: string; attrs: string[] } {
  const role = /^[^\s"[]+/.exec(key)?.[0] ?? key
  let rest = key.slice(role.length).trim()
  let name = ''
  if (rest.startsWith('"')) {
    const close = closingQuote(rest, 0)
    name = JSON.parse(rest.slice(0, close + 1)) as string
    rest = rest.slice(close + 1).trim()
  }
  const attrs = [...rest.matchAll(/\[([^\]]*)\]/g)].map((found) => found[1] as string)
  return { role, name, attrs }
}

/** Playwright's ARIA snapshot (its YAML), read into nodes. */
export function parseOutline(yaml: string): OutlineNode[] {
  const roots: OutlineNode[] = []
  const stack: { depth: number; node: OutlineNode }[] = []
  for (const raw of yaml.split('\n')) {
    const found = /^( *)- (.*)$/.exec(raw)
    if (!found) continue
    const depth = (found[1] as string).length / 2
    const content = found[2] as string
    while (stack.length && (stack.at(-1) as { depth: number }).depth >= depth) stack.pop()
    const parent = stack.at(-1)?.node
    if (content.startsWith('/')) {
      const { key, value } = keyAndValue(content)
      if (parent) parent.props[key.slice(1)] = value ?? ''
      continue
    }
    const { key, value } = keyAndValue(content)
    const { role, name, attrs } = readKey(key)
    const node: OutlineNode = { role, name, attrs, props: {}, children: [] }
    if (value !== undefined) node.text = value
    if (parent) parent.children.push(node)
    else roots.push(node)
    stack.push({ depth, node })
  }
  return roots
}

// MARK: - The differ

/** What a difference is about: a landmark, a control (a heading, tab, button, field, menu or dialog among them), or text. */
export type Kind = 'landmark' | 'control' | 'text'

const landmarks = new Set(['banner', 'main', 'navigation', 'complementary', 'contentinfo', 'region', 'form', 'search'])
const controls = new Set([
  'button',
  'link',
  'tab',
  'tablist',
  'tabpanel',
  'textbox',
  'searchbox',
  'combobox',
  'checkbox',
  'radio',
  'radiogroup',
  'switch',
  'slider',
  'spinbutton',
  'menu',
  'menubar',
  'menuitem',
  'menuitemcheckbox',
  'menuitemradio',
  'option',
  'listbox',
  'dialog',
  'alertdialog',
  'heading',
  'tree',
  'treeitem',
  'grid',
])

function kindOf(role: string): Kind {
  return landmarks.has(role) ? 'landmark' : controls.has(role) ? 'control' : 'text'
}

/** One line of an outline, depth-free: its identity is what two outlines are matched on. */
interface Line {
  role: string
  name: string
  attrs: string[]
  text?: string
  depth: number
  parent: number
  /** A name the node's own content makes (`button "local Gemma 4 12B …"` over its glyph and its words): matched by role, its words compared as text. */
  fromContent: boolean
  identity: string
  display: string
  within: string
}

const squash = (text: string) => text.replace(/\s+/g, ' ').trim()

/** The words a node's content says, as a name computed from content joins them. */
function contentOf(node: OutlineNode): string {
  const own = node.role === 'text' ? (node.text ?? '') : node.name || [node.text ?? '', ...node.children.map(contentOf)].filter(Boolean).join(' ')
  return own
}

function displayOf(line: Pick<Line, 'role' | 'name' | 'attrs' | 'text'>): string {
  if (line.role === 'text') return `text: ${JSON.stringify(line.text ?? '')}`
  const name = line.name ? ` ${JSON.stringify(line.name)}` : ''
  const attrs = line.attrs.map((attr) => ` [${attr}]`).join('')
  const text = line.text !== undefined ? `: ${JSON.stringify(line.text)}` : ''
  return `${line.role}${name}${attrs}${text}`
}

/** An ancestor as `within` names it: a node named by its content by its role alone, so the context is the same on both sides. */
function placeOf(line: Line): string {
  return line.fromContent ? `${line.role} (named by its content)` : `${line.role}${line.name ? ` ${JSON.stringify(line.name)}` : ''}`
}

/** An outline's nodes in document order, each a line with its depth, parent and context. */
function linesOf(nodes: OutlineNode[]): Line[] {
  const lines: Line[] = []
  const walk = (node: OutlineNode, depth: number, parent: number, within: string) => {
    const fromContent = node.name !== '' && node.children.length > 0 && squash(node.name) === squash([node.text ?? '', ...node.children.map(contentOf)].filter(Boolean).join(' '))
    const line: Line = {
      role: node.role,
      name: node.name,
      attrs: node.attrs,
      ...(node.text !== undefined ? { text: node.text } : {}),
      depth,
      parent,
      fromContent,
      identity: '',
      display: '',
      within,
    }
    line.identity = [line.role, line.attrs.join(','), fromContent ? '' : line.name, line.text ?? ''].join('\u0000')
    line.display = displayOf(line)
    const index = lines.push(line) - 1
    const place = within ? `${within} › ${placeOf(line)}` : placeOf(line)
    for (const child of node.children) walk(child, depth + 1, index, place)
  }
  for (const node of nodes) walk(node, 0, -1, '')
  return lines
}

/** The longest common subsequence of two key lists, as pairs of indexes. */
function common(a: readonly string[], b: readonly string[]): Array<[number, number]> {
  const n = a.length
  const m = b.length
  const table = new Int32Array((n + 1) * (m + 1))
  for (let i = n - 1; i >= 0; i -= 1) {
    for (let j = m - 1; j >= 0; j -= 1) {
      table[i * (m + 1) + j] = a[i] === b[j] ? (table[(i + 1) * (m + 1) + j + 1] as number) + 1 : Math.max(table[(i + 1) * (m + 1) + j] as number, table[i * (m + 1) + j + 1] as number)
    }
  }
  const pairs: Array<[number, number]> = []
  let i = 0
  let j = 0
  while (i < n && j < m) {
    if (a[i] === b[j]) {
      pairs.push([i, j])
      i += 1
      j += 1
    } else if ((table[(i + 1) * (m + 1) + j] as number) >= (table[i * (m + 1) + j + 1] as number)) i += 1
    else j += 1
  }
  return pairs
}

/** A text this long, changed, is said by the words that changed rather than whole. */
const longText = 160

/** The words that changed between two long texts, `git diff --word-diff`'s way, with three words around each change: `…its gaps: G1 to [-G24.-]{+G32.+}`. */
function wordChanges(old: string, now: string): string {
  const a = old.split(/\s+/).filter(Boolean)
  const b = now.split(/\s+/).filter(Boolean)
  const kept = common(a, b)
  const hunks: string[] = []
  let i = 0
  let j = 0
  const around = 3
  for (const [nextI, nextJ] of [...kept, [a.length, b.length] as [number, number]]) {
    if (nextI > i || nextJ > j) {
      const before = a.slice(Math.max(0, i - around), i).join(' ')
      const removed = a.slice(i, nextI).join(' ')
      const added = b.slice(j, nextJ).join(' ')
      const after = a.slice(nextI, Math.min(a.length, nextI + around)).join(' ')
      const lead = i - around > 0 ? '…' : ''
      const tail = nextI + around < a.length ? '…' : ''
      hunks.push(`${lead}${before}${before ? ' ' : ''}${removed ? `[-${removed}-]` : ''}${added ? `{+${added}+}` : ''}${after ? ` ${after}` : ''}${tail}`)
    }
    i = nextI + 1
    j = nextJ + 1
  }
  return hunks.join(' ')
}

/** One difference between the old outline and the new. */
export interface Difference {
  /** `missing` from the new side, `added` by it, `renamed` (its name or words changed) or a changed `state`. */
  change: 'missing' | 'added' | 'renamed' | 'state'
  kind: Kind
  role: string
  /** As the report prints it and a verdict matches it: `- button "x"`, `+ main "Models"`, `~ tab "a" → tab "b"`. */
  line: string
  /** The landmarks, regions and controls around it, outermost first: `complementary "Sidebar" › navigation "Places"`. */
  within: string
  /** A missing or added node's own lines, when its whole content went or came with it. */
  inner: string[]
}

const sameAttrs = (a: Line, b: Line) => a.attrs.join(',') === b.attrs.join(',')

/** A line's words, lowercased, its punctuation dropped. */
function wordsOf(line: Line): Set<string> {
  return new Set(`${line.name} ${line.text ?? ''}`.toLowerCase().match(/[\p{L}\p{N}]+/gu) ?? [])
}

/**
 * How alike two lines are: 0 for another role, else the share of their words
 * they have in common (1 for two with no words). A control or a landmark with
 * no word in common still pairs where nothing likelier does (a tab renamed is
 * the same tab); text with none is another text.
 */
function likeness(a: Line, b: Line): number {
  if (a.role !== b.role) return 0
  const x = wordsOf(a)
  const y = wordsOf(b)
  if (!x.size && !y.size) return 1
  const shared = [...x].filter((word) => y.has(word)).length
  if (shared === 0) return kindOf(a.role) === 'text' ? 0 : 0.001
  return shared / new Set([...x, ...y]).size
}

/** The pairs, in order, that make the two lists most alike: lines of a role with a word in common (`undefined`, a whole node, pairs with nothing). */
function alike(a: ReadonlyArray<Line | undefined>, b: ReadonlyArray<Line | undefined>): Array<[number, number]> {
  const n = a.length
  const m = b.length
  const score = new Float64Array((n + 1) * (m + 1))
  const weight = (i: number, j: number) => {
    const x = a[i]
    const y = b[j]
    return x && y ? likeness(x, y) : 0
  }
  for (let i = n - 1; i >= 0; i -= 1) {
    for (let j = m - 1; j >= 0; j -= 1) {
      const w = weight(i, j)
      score[i * (m + 1) + j] = Math.max(score[(i + 1) * (m + 1) + j] as number, score[i * (m + 1) + j + 1] as number, w > 0 ? w + (score[(i + 1) * (m + 1) + j + 1] as number) : 0)
    }
  }
  const pairs: Array<[number, number]> = []
  let i = 0
  let j = 0
  while (i < n && j < m) {
    const here = score[i * (m + 1) + j] as number
    const w = weight(i, j)
    if (w > 0 && here === w + (score[(i + 1) * (m + 1) + j + 1] as number)) {
      pairs.push([i, j])
      i += 1
      j += 1
    } else if (here === (score[(i + 1) * (m + 1) + j] as number)) i += 1
    else j += 1
  }
  return pairs
}

/** A run of lines nothing matched, one node and everything under it, or a lone line. */
interface Unit {
  top: number
  end: number
}

/** Which lines went (or came) whole: unmatched, and nothing under them matched. */
function wholly(lines: Line[], matched: Set<number>): boolean[] {
  const whole = new Array<boolean>(lines.length).fill(false)
  const anyMatched = new Array<boolean>(lines.length).fill(false)
  for (let index = lines.length - 1; index >= 0; index -= 1) {
    const line = lines[index] as Line
    whole[index] = !matched.has(index) && !anyMatched[index]
    if (line.parent >= 0 && (matched.has(index) || anyMatched[index])) anyMatched[line.parent] = true
  }
  return whole
}

/** The unmatched lines of one side between two matches, grouped: a node none of whose lines matched is one unit with its content. */
function unitsOf(lines: Line[], from: number, to: number, matched: Set<number>, lost: boolean[]): Unit[] {
  const units: Unit[] = []
  for (let index = from; index < to; index += 1) {
    if (matched.has(index)) continue
    let end = index + 1
    if (lost[index]) while (end < to && (lines[end] as Line).depth > (lines[index] as Line).depth) end += 1
    units.push({ top: index, end })
    index = end - 1
  }
  return units
}

/**
 * The differences between two outlines. Lines are matched depth-free (a
 * landmark around the same lines is the landmark alone), on their role,
 * state, words and name -- a name its own content makes left out, so a row
 * whose fixture changed shows its words as text, not itself as a control.
 * Between two matches, what is left pairs by role in order (a renamed tab, a
 * changed state; a landmark with a landmark), and the rest is missing or
 * added, a node whose whole content went with it as one difference.
 */
export function diffOutlines(old: string, now: string): Difference[] {
  const before = linesOf(parseOutline(old))
  const after = linesOf(parseOutline(now))
  const anchors = common(
    before.map((line) => line.identity),
    after.map((line) => line.identity),
  )
  const matchedBefore = new Set(anchors.map(([i]) => i))
  const matchedAfter = new Set(anchors.map(([, j]) => j))
  const lostBefore = wholly(before, matchedBefore)
  const lostAfter = wholly(after, matchedAfter)
  const differences: Difference[] = []
  const lone = (lines: Line[], unit: Unit, change: 'missing' | 'added'): Difference => {
    const line = lines[unit.top] as Line
    return {
      change,
      kind: kindOf(line.role),
      role: line.role,
      line: `${change === 'missing' ? '-' : '+'} ${line.display}`,
      within: line.within,
      inner: lines.slice(unit.top + 1, unit.end).map((each) => each.display),
    }
  }
  const bounds: Array<[number, number]> = [...anchors, [before.length, after.length]]
  let i = 0
  let j = 0
  for (const [nextI, nextJ] of bounds) {
    const olds = unitsOf(before, i, nextI, matchedBefore, lostBefore)
    const news = unitsOf(after, j, nextJ, matchedAfter, lostAfter)
    // Lone lines pair with their like, in order; whole nodes never pair.
    const paired = alike(
      olds.map((unit) => (unit.end === unit.top + 1 ? (before[unit.top] as Line) : undefined)),
      news.map((unit) => (unit.end === unit.top + 1 ? (after[unit.top] as Line) : undefined)),
    )
    const pairedOld = new Map(paired.map(([a, b]) => [a, b]))
    const pairedNew = new Set(paired.map(([, b]) => b))
    // A landmark left over pairs with a landmark left over: `~ banner → main "Models"`.
    const leftOld = olds.map((_, index) => index).filter((index) => !pairedOld.has(index) && kindOf((before[(olds[index] as Unit).top] as Line).role) === 'landmark' && (olds[index] as Unit).end === (olds[index] as Unit).top + 1)
    const leftNew = news.map((_, index) => index).filter((index) => !pairedNew.has(index) && kindOf((after[(news[index] as Unit).top] as Line).role) === 'landmark' && (news[index] as Unit).end === (news[index] as Unit).top + 1)
    for (let k = 0; k < Math.min(leftOld.length, leftNew.length); k += 1) {
      pairedOld.set(leftOld[k] as number, leftNew[k] as number)
      pairedNew.add(leftNew[k] as number)
    }
    const emitted = new Set<number>()
    const emitNewBefore = (limit: number) => {
      for (let b = 0; b < limit; b += 1) {
        if (pairedNew.has(b) || emitted.has(b)) continue
        emitted.add(b)
        differences.push(lone(after, news[b] as Unit, 'added'))
      }
    }
    for (const [a, unit] of olds.entries()) {
      const b = pairedOld.get(a)
      if (b === undefined) {
        differences.push(lone(before, unit, 'missing'))
        continue
      }
      emitNewBefore(b)
      emitted.add(b)
      const was = before[unit.top] as Line
      const is = after[(news[b] as Unit).top] as Line
      const sameWords = was.role === is.role && was.name === is.name && was.text === is.text
      const wordy = was.role === is.role && was.name === is.name && sameAttrs(was, is) && was.text !== undefined && is.text !== undefined && Math.max(was.text.length, is.text.length) > longText
      differences.push({
        change: sameWords ? 'state' : 'renamed',
        kind: kindOf(was.role),
        role: was.role,
        line: wordy ? `~ ${was.role}${was.name ? ` ${JSON.stringify(was.name)}` : ''}: ${wordChanges(was.text as string, is.text as string)}` : `~ ${was.display} → ${is.display}`,
        within: was.within,
        inner: [],
      })
    }
    emitNewBefore(news.length)
    i = nextI + 1
    j = nextJ + 1
  }
  return differences
}

// MARK: - The verdicts

/** One verdict in parity/verdicts.json: the differences it classes, and why. */
export interface Verdict {
  /** The pairs it covers: `models@installed`, `models` (every mode), or a pattern with `*` (`library*@missing`, `*`). */
  presets: string[]
  /** A pattern the difference's line must match (`~ tab "Stored weights \\d+" → …`). */
  match?: string
  /** Or the differences' lines exactly, as the report prints them. */
  lines?: string[]
  /** A pattern its `within` must match, where the same words mean different things in different regions. */
  within?: string
  class: 'data' | 'fix' | 'defect'
  /** `data`: the recording, or recordings, the new side draws (a file name in a fixture root). */
  fixture?: string | string[]
  /** `fix`: the design item whose shipped record names the change (`1h`). */
  item?: string
  reason: string
}

export interface VerdictFile {
  $comment?: string
  verdicts: Verdict[]
}

export function readVerdicts(file: string): VerdictFile {
  return JSON.parse(readFileSync(file, 'utf8')) as VerdictFile
}

/** What a verdict is checked against: MILESTONES.md's records, and the fixture roots' files. */
export interface RepoContext {
  milestones: string
  fixtureExists(name: string): boolean
}

/** Every file under a folder, by its path from the folder. */
function filesUnder(folder: string): string[] {
  if (!existsSync(folder)) return []
  return (readdirSync(folder, { recursive: true, withFileTypes: true }) as Array<{ isFile(): boolean; name: string; parentPath: string }>)
    .filter((entry) => entry.isFile())
    .map((entry) => relative(folder, join(entry.parentPath, entry.name)))
}

/** The real MILESTONES.md and the two fixture roots: the design's `fixtures/` and the Mac app's `Fixtures/` folders. */
export function repoContext(prototype: string): RepoContext {
  const repo = resolve(prototype, '../../..')
  const milestones = readFileSync(join(repo, 'lib/documentation/assistant/design/MILESTONES.md'), 'utf8')
  const files = [
    ...filesUnder(join(prototype, 'fixtures')),
    ...filesUnder(join(repo, 'lib/src/darwin/tests'))
      .filter((file) => file.includes('Fixtures/'))
      .map((file) => file),
  ]
  const names = new Set(files.map((file) => basename(file)))
  return { milestones, fixtureExists: (name) => names.has(name) || files.some((file) => file.endsWith(`/${name}`)) }
}

const verdictName = (verdict: Verdict, index: number) => `verdict ${index + 1} (${verdict.match ?? verdict.lines?.[0] ?? 'no lines'})`

/**
 * Lines no record names, planted where a verdict's `match` is tried: a lost
 * or added button, a lost tab, a renamed heading, a lost dialog or alert. A
 * pattern that classes one would class any such loss on every pair it covers,
 * so the proof could not see a control go; a verdict lists the lines it
 * classes, or a pattern that carries their words (a fixture's ids or names).
 */
const canary = '⁂ ⁑ 0713 ⁂'
export const canaries: readonly string[] = [
  `- button "${canary}"`,
  `+ button "${canary}"`,
  `- tab "${canary}"`,
  `~ heading "${canary}" [level=2] → heading "⁂ ⁑ 0714 ⁂" [level=2]`,
  `- dialog "${canary}"`,
  `- alertdialog "${canary}"`,
]

/** Why a verdict cannot stand: its class, reason, presets and pattern, a fix's item, a data verdict's fixture. */
export function verdictProblems(verdicts: Verdict[], context: RepoContext): string[] {
  const problems: string[] = []
  for (const [index, verdict] of verdicts.entries()) {
    const say = (what: string) => problems.push(`${verdictName(verdict, index)}: ${what}`)
    if (!['data', 'fix', 'defect'].includes(verdict.class)) say('its class must be data, fix or defect')
    if (!verdict.reason?.trim()) say('it gives no reason')
    if (!verdict.presets?.length) say('it names no preset')
    if (verdict.match === undefined && !verdict.lines?.length) say('it has neither a match nor lines')
    for (const [field, pattern] of [
      ['match', verdict.match],
      ['within', verdict.within],
    ] as const) {
      if (pattern === undefined) continue
      try {
        new RegExp(pattern, 'u')
      } catch (error) {
        say(`its ${field} is not a pattern: ${(error as Error).message}`)
      }
    }
    if (verdict.match !== undefined) {
      let pattern: RegExp | undefined
      try {
        pattern = new RegExp(verdict.match, 'u')
      } catch {
        pattern = undefined
      }
      const classed = pattern ? canaries.find((line) => (pattern as RegExp).test(line)) : undefined
      if (classed !== undefined) say(`its match classes a planted line no record names (${classed}): list the lines it classes, or a pattern that carries their words`)
    }
    if (verdict.class === 'fix') {
      if (!verdict.item) say('a fix cites no item (its "item", as "1h")')
      else if (!context.milestones.includes(`(backlog item ${verdict.item})`)) say(`it cites item ${verdict.item}, which MILESTONES.md records no "(backlog item ${verdict.item})" for`)
    }
    if (verdict.class === 'data') {
      const fixtures = verdict.fixture === undefined ? [] : [verdict.fixture].flat()
      if (!fixtures.length) say('a data verdict names no fixture (its "fixture")')
      for (const fixture of fixtures) if (!context.fixtureExists(fixture)) say(`its fixture ${fixture} is in neither fixture root`)
    }
  }
  return problems
}

/** Whether a verdict's presets cover a pair: its id, its preset in any mode, or a pattern with `*`. */
export function covers(patterns: readonly string[], id: string): boolean {
  return patterns.some((pattern) => {
    if (pattern.includes('*')) return new RegExp(`^${pattern.split('*').map((part) => part.replace(/[.+?^${}()|[\]\\]/g, '\\$&')).join('.*')}$`, 'u').test(id)
    return id === pattern || id.startsWith(`${pattern}@`)
  })
}

/** One pair, both outlines read. */
export interface PairOutlines {
  id: string
  preset?: string
  mode?: string
  old: string
  now: string
}

export interface ComparedPair {
  id: string
  preset?: string
  mode?: string
  differences: Difference[]
}

export function comparePairs(pairs: PairOutlines[]): ComparedPair[] {
  return pairs.map((pair) => ({ id: pair.id, ...(pair.preset ? { preset: pair.preset } : {}), ...(pair.mode ? { mode: pair.mode } : {}), differences: diffOutlines(pair.old, pair.now) }))
}

/** A difference and the verdict that classes it (the first in the file that covers it). */
export interface Judged {
  difference: Difference
  verdict?: Verdict
  /** The verdict's place in the file, from 1. */
  number?: number
}

/** One named check of a run, and what it found wrong. */
export interface Check {
  name: string
  problems: string[]
  /** Why the check could not run (nothing to compare it with): said in place of a pass. */
  skipped?: string
}

export interface Run {
  pairs: Array<{ id: string; preset?: string; mode?: string; judged: Judged[] }>
  checks: Check[]
}

/** Every difference matched to the first verdict covering it; a difference with none, a defect, and a verdict that classes nothing each fail. */
export function judge(pairs: ComparedPair[], verdicts: Verdict[]): Run {
  // A pattern that does not compile classes nothing: verdictProblems says why.
  const compile = (pattern: string | undefined) => {
    if (pattern === undefined) return undefined
    try {
      return new RegExp(pattern, 'u')
    } catch {
      return /(?!)/u
    }
  }
  const compiled = verdicts.map((verdict) => {
    const pattern = compile(verdict.match)
    const lines = new Set(verdict.lines ?? [])
    return { verdict, test: (line: string) => lines.has(line) || (pattern?.test(line) ?? false), within: compile(verdict.within) }
  })
  const used = new Set<number>()
  const unjudged: string[] = []
  const defects: string[] = []
  const judged = pairs.map((pair) => ({
    id: pair.id,
    ...(pair.preset ? { preset: pair.preset } : {}),
    ...(pair.mode ? { mode: pair.mode } : {}),
    judged: pair.differences.map((difference): Judged => {
      const found = compiled.findIndex((each) => covers(each.verdict.presets, pair.id) && each.test(difference.line) && (each.within === undefined || each.within.test(difference.within)))
      const where = difference.within ? ` (in ${difference.within})` : ''
      if (found < 0) {
        unjudged.push(`${pair.id}: no verdict for ${difference.line}${where}`)
        return { difference }
      }
      used.add(found)
      const verdict = (compiled[found] as { verdict: Verdict }).verdict
      if (verdict.class === 'defect') defects.push(`${pair.id}: a defect is open: ${difference.line} (${verdict.reason})`)
      return { difference, verdict, number: found + 1 }
    }),
  }))
  const unused = verdicts.flatMap((verdict, index) => (used.has(index) ? [] : [`${verdictName(verdict, index)}: it classes no difference`]))
  return {
    pairs: judged,
    checks: [
      { name: 'every difference has a verdict', problems: unjudged },
      { name: 'no defect is open', problems: defects },
      { name: 'every verdict classes a difference', problems: unused },
    ],
  }
}

// MARK: - The presets and the pairs

/** The new app's start presets: its board table's starts, which 1q's test holds to the preset table, one frame each. */
export function newPresets(): string[] {
  return boards.flatMap((entry) => (entry.draws === 'screen' && entry.start !== undefined ? [entry.start] : []))
}

/** A preset baseline.json lists that the new app cannot open. */
export function counterpartProblems(baselinePresets: readonly string[], presets: readonly string[]): string[] {
  return baselinePresets.filter((preset) => !presets.includes(preset)).map((preset) => `the preset ${preset} in baseline.json has no counterpart in the new app: no frame in gallery/Models/Boards.ts starts at it`)
}

/**
 * The presets the old prototype opens by no start: reached by a scripted
 * click from a preset it does open. `training:regimes` -- the old
 * `applyStart()` ignores a `training` sub it does not know and opens Runs
 * (`main.html:2782-2783` at `b375fac9`), so its old side is `training`,
 * still, with the Regimes button clicked: a pressed-state button, not a tab
 * (`aria-pressed="{{trn.regOn}}"`, `main.html:1288`).
 */
export const reachedByClick: Readonly<Record<string, { start: string; click: string }>> = {
  'training:regimes': { start: 'training', click: 'Regimes' },
}

/** The 10 boards rebuilt in the gallery (1q's foundations, 1r's Build pages): Exploration's five have no new side. */
export const rebuiltBoards = ['BuildContract', 'BuildLayout', 'BuildProcesses', 'ColorTokens', 'TypeSpace', 'ComponentsModel', 'ComponentsTurn', 'ComponentsPrompts', 'ComponentsMedia', 'Logo'] as const

/** One pair to compare: what the old side opens and what the new side opens. */
export interface Pair {
  id: string
  kind: 'screen' | 'board'
  preset?: string
  mode?: string
  old: {
    /** A screen's start (the click's start, for one reached by a click). */
    start?: string
    click?: string
    /** A board's file and its query. */
    board?: string
    query?: string
    /** Its image in 1c's baseline folder, where 1c shot it. */
    image?: string
  }
  now: { start?: string; board?: string; theme?: string; title?: string }
}

/**
 * Every pair: each preset in baseline.json in each `apogee` mode 1c kept,
 * then each preset the new app adds at `installed` (1c kept no other mode of
 * it), then the 10 rebuilt boards at each prop value 1c shot them in, the
 * gallery's focused board in the theme that value names (a board with no
 * prop menu is dark, the Build boards' only theme).
 */
export function pairsOf(baseline: Pick<Baseline, 'presets' | 'modes'>, presets: readonly string[]): Pair[] {
  const pairs: Pair[] = []
  for (const preset of baseline.presets) {
    for (const mode of baseline.modes[preset] ?? ['installed']) {
      pairs.push({
        id: `${preset}@${mode}`,
        kind: 'screen',
        preset,
        mode,
        old: { start: preset, image: `screens/${fileName(preset)}@${mode}.png` },
        now: { start: preset },
      })
    }
  }
  for (const preset of presets.filter((each) => !baseline.presets.includes(each))) {
    const click = reachedByClick[preset]
    pairs.push({ id: `${preset}@installed`, kind: 'screen', preset, mode: 'installed', old: click ? { start: click.start, click: click.click } : { start: preset }, now: { start: preset } })
  }
  for (const id of rebuiltBoards) {
    const entry = boards.find((board) => board.id === id)
    if (!entry) throw new Refusal(`the gallery's board table has no ${id}`)
    const menus = Object.entries(entry.props ?? {})
    const values = menus.length ? menus.flatMap(([name, options]) => options.map((value) => ({ name, value }))) : [undefined]
    // 1c shot each menu value in the old file's order: dark, light, then contrast for the Components boards.
    const order = ['dark', 'light', 'contrast']
    values.sort((a, b) => (a && b ? order.indexOf(a.value) - order.indexOf(b.value) : 0))
    for (const value of values) {
      const suffix = value ? `@${fileName(value.name)}=${fileName(value.value)}` : ''
      pairs.push({
        id: `${id}${suffix}`,
        kind: 'board',
        old: { board: `${id}.dc.html`, query: value ? `?${encodeURIComponent(value.name)}=${encodeURIComponent(value.value)}` : '', image: `boards/${id}${suffix}.png` },
        now: { board: id, theme: value?.value ?? 'dark', title: entry.title },
      })
    }
  }
  return pairs
}

// MARK: - Two runs

/** The outlines a run read, each by its SHA-256, under the build they were read from. */
export function outlinesList(built: string, files: Array<{ file: string; text: string }>): string {
  return `# built ${built}\n${[...files]
    .sort((a, b) => (a.file < b.file ? -1 : a.file > b.file ? 1 : 0))
    .map((each) => `${createHash('sha256').update(each.text).digest('hex')}  ${each.file}\n`)
    .join('')}`
}

/** The same build read twice gives the same outlines: a run against the last run's list. A first run, or a new build, has nothing to compare with, and says so. */
export function againCheck(previous: string | undefined, now: string): Check {
  const name = 'the same build reads the same outlines twice'
  if (previous === undefined) return { name, problems: [], skipped: 'not compared: no earlier run; run npm run parity again on this build' }
  const builtOf = (text: string) => /^# built (.*)$/m.exec(text)?.[1]
  if (builtOf(previous) !== builtOf(now)) return { name, problems: [], skipped: `not compared: the last run read the build ${builtOf(previous) ?? 'unknown'}; run npm run parity again on this build` }
  const read = (text: string) => new Map(text.split('\n').filter((line) => line && !line.startsWith('#')).map((line) => [line.slice(66), line.slice(0, 64)] as const))
  const before = read(previous)
  const after = read(now)
  const differ = [...new Set([...before.keys(), ...after.keys()])].filter((file) => before.get(file) !== after.get(file)).sort()
  return { name, problems: differ.length ? [`the same build (${builtOf(now)}) gave different outlines from the last run: ${differ.join(', ')}`] : [] }
}

// MARK: - The report

function escape(text: string): string {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')
}

function counts(judged: Judged[]) {
  const data = judged.filter((each) => each.verdict?.class === 'data').length
  const fixes = judged.filter((each) => each.verdict?.class === 'fix')
  const items = [...new Set(fixes.map((each) => each.verdict?.item as string))].sort()
  const open = judged.filter((each) => !each.verdict || each.verdict.class === 'defect').length
  return { data, fix: fixes.length, items, open }
}

/** The summary table, as Markdown: each pair, its differences by class, the items its fixes cite. */
export function summaryTable(run: Run): string {
  const rows = run.pairs.map((pair) => {
    const c = counts(pair.judged)
    return `| \`${pair.id}\` | ${pair.judged.length} | ${c.data} | ${c.fix}${c.items.length ? ` (${c.items.join(', ')})` : ''} | ${c.open} |`
  })
  const all = counts(run.pairs.flatMap((pair) => pair.judged))
  const total = run.pairs.reduce((sum, pair) => sum + pair.judged.length, 0)
  return ['| Pair | Differences | data | fix | Unjudged |', '|---|---|---|---|---|', ...rows, `| **All ${run.pairs.length}** | ${total} | ${all.data} | ${all.fix} | ${all.open} |`, ''].join('\n')
}

/** What the report says beside the run: the revision, the build, and each pair's two images. */
export interface ReportMeta {
  revision: string
  built: string
  images: Record<string, { old?: string; now?: string }>
  /** What each item's record is called in MILESTONES.md, for the fixes' citations. */
  records?: Record<string, string>
  notes?: string[]
}

/** The report page: the checks, the summary, then each pair side by side with its differences, each classed with its reason. */
export function report(run: Run, meta: ReportMeta): string {
  const failing = run.checks.filter((check) => check.problems.length)
  const skipped = run.checks.some((check) => !check.problems.length && check.skipped !== undefined)
  const verdictCell = (judged: Judged) => {
    const verdict = judged.verdict
    if (!verdict) return '<td class="open">no verdict</td><td></td>'
    const cite =
      verdict.class === 'fix'
        ? `${escape(verdict.item ?? '')}${meta.records?.[verdict.item ?? ''] ? ` · ${escape(meta.records[verdict.item ?? ''] as string)}` : ''}`
        : verdict.class === 'data'
          ? [verdict.fixture ?? []].flat().map((fixture) => `<code>${escape(fixture)}</code>`).join(', ')
          : ''
    return `<td class="${escape(verdict.class)}">${escape(verdict.class)}</td><td>${escape(verdict.reason)}${cite ? `<div class="cite">${cite} · verdict ${judged.number}</div>` : ''}</td>`
  }
  const section = (pair: Run['pairs'][number]) => {
    const images = meta.images[pair.id] ?? {}
    const c = counts(pair.judged)
    const figure = (src: string | undefined, label: string) =>
      src ? `<figure><figcaption>${label}</figcaption><a href="${escape(src)}"><img src="${escape(src)}" loading="lazy" alt="${escape(`${pair.id}, ${label}`)}"></a></figure>` : `<figure><figcaption>${label}</figcaption><p>No picture.</p></figure>`
    const rows = pair.judged
      .map(
        (judged) =>
          `<tr><td><code>${escape(judged.difference.line)}</code>${judged.difference.within ? `<div class="within">in ${escape(judged.difference.within)}</div>` : ''}${
            judged.difference.inner.length ? `<details><summary>${judged.difference.inner.length} lines with it</summary><pre>${escape(judged.difference.inner.join('\n'))}</pre></details>` : ''
          }</td>${verdictCell(judged)}</tr>`,
      )
      .join('\n')
    return `<section id="${escape(pair.id)}">
<h2>${escape(pair.id)} <span class="tally">${pair.judged.length} differences · ${c.data} data · ${c.fix} fix${c.open ? ` · <b>${c.open} open</b>` : ''}</span></h2>
<div class="pair">${figure(images.old, 'The old prototype')}${figure(images.now, 'The built file')}</div>
${pair.judged.length ? `<table><thead><tr><th>Difference</th><th>Class</th><th>Reason</th></tr></thead><tbody>\n${rows}\n</tbody></table>` : '<p>The outlines are the same.</p>'}
</section>`
  }
  const all = counts(run.pairs.flatMap((pair) => pair.judged))
  const total = run.pairs.reduce((sum, pair) => sum + pair.judged.length, 0)
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Parity proof</title>
<style>
:root { color-scheme: light dark; --bg: #f6f6f4; --fg: #1d1d1b; --muted: #5d5d58; --line: #d9d9d4; --ok: #1d6b3a; --bad: #a3261b; --fix: #3b4fb8; }
@media (prefers-color-scheme: dark) { :root { --bg: #161615; --fg: #ececea; --muted: #a3a39e; --line: #383836; --ok: #7fd49a; --bad: #f2836f; --fix: #a7b1f8; } }
body { margin: 0; padding: 24px 16px 64px; background: var(--bg); color: var(--fg); font: 14px/1.45 system-ui, sans-serif; }
header, section, nav { max-width: 1600px; margin: 0 auto; }
h1 { font-size: 20px; margin: 0 0 4px; }
h2 { font-size: 16px; margin: 40px 0 12px; scroll-margin-top: 16px; }
p { margin: 4px 0; color: var(--muted); }
code, pre { font: 12px ui-monospace, Menlo, monospace; }
pre { white-space: pre-wrap; margin: 4px 0; }
.tally { font-weight: 400; color: var(--muted); font-size: 13px; }
.pair { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 480px), 1fr)); gap: 16px; }
figure { margin: 0; min-width: 0; }
figcaption { font-size: 12px; color: var(--muted); margin-bottom: 4px; }
figure img { display: block; width: 100%; height: auto; max-height: 720px; object-fit: cover; object-position: top; border: 1px solid var(--line); border-radius: 4px; }
table { width: 100%; border-collapse: collapse; margin-top: 12px; table-layout: fixed; }
th, td { text-align: left; vertical-align: top; padding: 6px 8px; border-top: 1px solid var(--line); overflow-wrap: anywhere; }
th:nth-child(1) { width: 52%; } th:nth-child(2) { width: 8%; }
td.data { color: var(--ok); } td.fix { color: var(--fix); } td.open, td.defect { color: var(--bad); font-weight: 600; }
.within, .cite { font-size: 12px; color: var(--muted); }
.checks li.bad { color: var(--bad); }
.checks li.skip { color: var(--muted); }
nav ol { columns: 4 220px; font-size: 13px; }
@media (max-width: 600px) { th:nth-child(1) { width: 60%; } }
</style>
</head>
<body>
<header>
<h1>The parity proof</h1>
<p>The old prototype at <code>${escape(meta.revision.slice(0, 8))}</code> (1c's baseline) beside the built <code>lib/design/main.html</code> (sources <code>${escape(meta.built)}</code>), ${run.pairs.length} pairs at 1440 × 900, still. ${total} differences in their outlines: ${all.data} the CLI's data, ${all.fix} fixes a shipped item records${all.open ? `, <b>${all.open} open</b>` : ''}. Made by <code>npm run parity</code> in <code>lib/design/prototype</code>; the outlines are the measure, the pictures are for the eye.</p>
${(meta.notes ?? []).map((note) => `<p>${escape(note)}</p>`).join('\n')}
<ul class="checks">
${run.checks
  .map(
    (check) =>
      `<li class="${check.problems.length ? 'bad' : check.skipped !== undefined ? 'skip' : 'ok'}">${escape(check.name)}: ${
        check.problems.length ? `${check.problems.length} problem${check.problems.length === 1 ? '' : 's'}<pre>${escape(check.problems.join('\n'))}</pre>` : check.skipped !== undefined ? escape(check.skipped) : 'yes'
      }</li>`,
  )
  .join('\n')}
</ul>
${failing.length ? '' : skipped ? '<p>Every check that ran passes.</p>' : '<p>Every check passes.</p>'}
</header>
<nav><h2>The pairs</h2><ol>
${run.pairs.map((pair) => `<li><a href="#${escape(pair.id)}">${escape(pair.id)}</a> (${pair.judged.length})</li>`).join('\n')}
</ol></nav>
${run.pairs.map(section).join('\n')}
</body>
</html>
`
}

// MARK: - Reading a page

/** A page's outline: Playwright's ARIA snapshot of the body, or of the element `selector` names. */
export async function outlineOf(page: Page, selector = 'body'): Promise<string> {
  return page.locator(selector).first().ariaSnapshot()
}

/** Waits for the built file to settle: its fonts, then no change to the page for a quarter second, then two frames. */
async function settled(page: Page): Promise<void> {
  await page.evaluate(async () => {
    await document.fonts.ready
    await new Promise<void>((done) => {
      let timer: ReturnType<typeof setTimeout>
      const finish = () => {
        observer.disconnect()
        done()
      }
      const observer = new MutationObserver(() => {
        clearTimeout(timer)
        timer = setTimeout(finish, 250)
      })
      observer.observe(document, { subtree: true, childList: true, attributes: true, characterData: true })
      timer = setTimeout(finish, 250)
      setTimeout(finish, 10_000)
    })
    await document.fonts.ready
    await new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)))
  })
}

/** One side's frame: its outline and its picture. */
interface Frame {
  outline: string
  png: Buffer
}

/**
 * The old side of a pair, in 1c's frame: a page of its own, loaded once for
 * its fonts, held to the pins, loaded again and shot, as `baseline.ts`'s
 * `shoot` does -- then the click, where the pair is reached by one, and the
 * outline read from the same page.
 */
async function oldFrame(session: Session, pins: Pins, url: string, what: string, size: { width: number; height: number }, fullPage: boolean, click?: string): Promise<Frame> {
  session.fontFailures.length = 0
  const page = await session.context.newPage()
  try {
    await page.setViewportSize(size)
    await checkFonts(session, await load(page, url, what), what, pins)
    const found = await load(page, url, what)
    if (found.errorBox !== null) throw new Refusal(`${what} drew support.js's error box: ${found.errorBox}`)
    await checkFonts(session, found, what, pins)
    if (click !== undefined) {
      await page.getByRole('button', { name: click, exact: true }).click()
      await page.evaluate(() => new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done))))
    }
    const png = await page.screenshot({ fullPage, animations: 'disabled', caret: 'hide', scale: 'css' })
    return { outline: await outlineOf(page), png }
  } finally {
    await page.close()
  }
}

/** What the built file did that a double-click must not: a console error, or a request for anything but itself. */
interface Watched {
  errors: string[]
  requests: string[]
}

/**
 * The new side of a pair: the built file from disk in 1c's frame (the same
 * viewport, clock, time zone, locale and seed), in a page of its own on its
 * second load, settled. A screen is the viewport; a board is the gallery's
 * focused board, its outline and picture the board alone.
 */
async function newFrame(browser: Browser, settings: Pick<Baseline, 'clock' | 'timezone' | 'locale' | 'seed'>, url: string, board: string | undefined, watched: Watched): Promise<Frame> {
  const context = await browser.newContext({ viewport: board ? { width: 1600, height: 900 } : viewport, deviceScaleFactor: 1, timezoneId: settings.timezone, locale: settings.locale, colorScheme: 'light' })
  try {
    await context.clock.setFixedTime(new Date(settings.clock))
    await context.addInitScript(prepareWindow, settings.seed)
    const page = await context.newPage()
    page.on('console', (message) => {
      if (message.type() === 'error') watched.errors.push(`${url}: ${message.text()}`)
    })
    page.on('pageerror', (error) => watched.errors.push(`${url}: ${error.message}`))
    page.on('request', (request) => {
      const asked = request.url()
      if (!asked.startsWith('data:') && asked.split(/[?#]/)[0] !== url.split(/[?#]/)[0]) watched.requests.push(`${url}: ${asked}`)
    })
    await page.goto(url, { waitUntil: 'load' })
    await settled(page)
    await page.goto(url, { waitUntil: 'load' })
    await settled(page)
    if (board === undefined) {
      const png = await page.screenshot({ animations: 'disabled', caret: 'hide', scale: 'css' })
      return { outline: await outlineOf(page), png }
    }
    const body = page.getByRole('region', { name: board, exact: true }).locator(':scope > div').last().locator(':scope > div').first()
    const png = await body.screenshot({ animations: 'disabled', caret: 'hide', scale: 'css' })
    return { outline: await body.ariaSnapshot(), png }
  } finally {
    await context.close()
  }
}

// MARK: - The run

export interface ParityOptions {
  repo: string
  prototype: string
  log: (line: string) => void
}

/** `<hash>  <path>` lines, by path. */
function readHashList(file: string): Map<string, string> {
  if (!existsSync(file)) return new Map()
  return new Map(
    readFileSync(file, 'utf8')
      .split('\n')
      .filter(Boolean)
      .map((line) => [line.slice(66), line.slice(0, 64)] as const),
  )
}

/** Whether 1c's folder holds every image its hash list names, each with its hash. */
function baselineIntact(folder: string): boolean {
  const list = readHashList(join(folder, 'hashes.txt'))
  if (!list.size) return false
  return [...list].every(([file, hash]) => existsSync(join(folder, file)) && sha256(readFileSync(join(folder, file))) === hash)
}

/** The build's source hash, from the built file's own meta tag. */
export function builtHash(file: string): string {
  return /<meta name="apogee-design-sources" content="([^"]+)"/.exec(readFileSync(file, 'utf8'))?.[1] ?? 'unknown'
}

/** Each design item's record heading in MILESTONES.md, by its id. */
function recordsOf(milestones: string): Record<string, string> {
  const records: Record<string, string> = {}
  for (const found of milestones.matchAll(/^### (\d{4}-\d{2}-\d{2}) — `([^`]+)` \(backlog item (\w+)\): (.*)$/gm)) records[found[3] as string] = `MILESTONES.md, ${found[1]} — ${found[2]}`
  return records
}

/**
 * The run: 1c's images checked against its hash list (re-made by its own
 * capture where they do not match), both sides of every pair captured and
 * read, the outlines diffed and judged, the outlines compared with the last
 * run's of the same build, and the report written to `.parity/report/`.
 */
export async function parity(options: ParityOptions): Promise<Run> {
  const { repo, prototype, log } = options
  const baselineFile = join(prototype, 'parity/baseline.json')
  const baseline = JSON.parse(readFileSync(baselineFile, 'utf8')) as Baseline
  if (!isReachable(repo, baseline.revision)) throw new Refusal(`the recorded revision ${baseline.revision} is not in this repository's history`)
  if (specTree(repo, baseline.revision) !== baseline.tree) throw new Refusal(`lib/design/spec at ${baseline.revision} is not the tree baseline.json records`)
  if (playwrightVersion() !== baseline.playwright) throw new Refusal(`baseline.json was shot with Playwright ${baseline.playwright}, and ${playwrightVersion()} is installed`)
  const baselineDir = join(prototype, '.parity/baseline', baseline.revision)
  if (baselineIntact(baselineDir)) log(`1c's images: reused, every one its hash list's (${readHashList(join(baselineDir, 'hashes.txt')).size})`)
  else {
    log("1c's images do not match their hash list: re-making them with npm run baseline's capture")
    await captureBaseline({ repo, baselineFile, outRoot: join(prototype, '.parity/baseline'), log })
  }
  const hashes = readHashList(join(baselineDir, 'hashes.txt'))
  const built = join(prototype, '../main.html')
  const builtUrl = pathToFileURL(built).href
  const reportDir = join(prototype, '.parity/report')
  const presets = newPresets()
  const pairs = pairsOf(baseline, presets)
  const frames: PairOutlines[] = []
  const images: ReportMeta['images'] = {}
  const oldFrames: string[] = []
  const watched: Watched = { errors: [], requests: [] }
  for (const folder of ['old', 'new', 'outlines']) rmSync(join(reportDir, folder), { recursive: true, force: true })
  const save = (file: string, bytes: Buffer | string) => {
    mkdirSync(dirname(join(reportDir, file)), { recursive: true })
    writeFileSync(join(reportDir, file), bytes)
  }
  await inArchive(repo, baseline.revision, async (html) => {
    const canvas = JSON.parse(readFileSync(join(html, '../json/canvas.json'), 'utf8')) as { boards: Record<string, { w: number; h: number }> }
    const server = await serve(html)
    const browser = await chromium.launch({ args: browserArgs })
    try {
      let session = await openSession(browser, baseline, undefined)
      const pins: Pins = { fonts: { ...baseline.fonts }, recording: false }
      const take = async (...args: Parameters<typeof oldFrame> extends [Session, Pins, ...infer Rest] ? Rest : never): Promise<Frame> => {
        for (let attempt = 1; ; attempt += 1) {
          try {
            return await oldFrame(session, pins, ...args)
          } catch (error) {
            if (!(error instanceof Unpinned) || attempt >= pinTries) throw error
            log(`${args[1]}: ${error.message}; taking it again in a fresh context`)
            await session.context.close()
            session = await openSession(browser, baseline, undefined)
          }
        }
      }
      for (const [index, pair] of pairs.entries()) {
        const what = `${pair.id} (old)`
        const old =
          pair.kind === 'screen'
            ? await take(`${server.origin}/main.html?start=${encodeURIComponent(pair.old.start as string)}&still=true&apogee=${pair.mode}`, what, viewport, false, pair.old.click)
            : await take(`${server.origin}/${pair.old.board}${pair.old.query}`, what, canvas.boards[pair.old.board as string] ? { width: (canvas.boards[pair.old.board as string] as { w: number }).w, height: (canvas.boards[pair.old.board as string] as { h: number }).h } : viewport, true)
        const oldImage = pair.old.image
        if (oldImage !== undefined) {
          const recorded = hashes.get(oldImage)
          if (recorded !== sha256(old.png)) oldFrames.push(`${pair.id}: the old frame was not 1c's: its picture hashes ${sha256(old.png).slice(0, 12)}, where ${oldImage} in 1c's hash list is ${recorded?.slice(0, 12) ?? 'missing'}`)
          mkdirSync(dirname(join(reportDir, 'old', oldImage)), { recursive: true })
          copyFileSync(join(baselineDir, oldImage), join(reportDir, 'old', oldImage))
        } else save(`old/screens/${fileName(pair.id)}.png`, old.png)
        const nowUrl = pair.kind === 'screen' ? `${builtUrl}?start=${pair.now.start}&still=true&apogee=${pair.mode}` : `${builtUrl}?gallery&board=${pair.now.board}&theme=${pair.now.theme}`
        const now = await newFrame(browser, baseline, nowUrl, pair.kind === 'board' ? pair.now.title : undefined, watched)
        const imageName = pair.kind === 'screen' ? `screens/${fileName(pair.id)}.png` : `boards/${fileName(pair.id)}.png`
        save(`new/${imageName}`, now.png)
        save(`outlines/${fileName(pair.id)}.old.yml`, old.outline)
        save(`outlines/${fileName(pair.id)}.new.yml`, now.outline)
        images[pair.id] = { old: oldImage !== undefined ? `old/${oldImage}` : `old/screens/${fileName(pair.id)}.png`, now: `new/${imageName}` }
        frames.push({ id: pair.id, ...(pair.preset ? { preset: pair.preset } : {}), ...(pair.mode ? { mode: pair.mode } : {}), old: old.outline, now: now.outline })
        log(`[${index + 1}/${pairs.length}] ${pair.id}`)
      }
    } finally {
      await browser.close()
      server.stop()
    }
  })
  const listFile = join(reportDir, 'outlines.txt')
  const previous = existsSync(listFile) ? readFileSync(listFile, 'utf8') : undefined
  const source = builtHash(built)
  const list = outlinesList(
    source,
    frames.flatMap((frame) => [
      { file: `outlines/${fileName(frame.id)}.old.yml`, text: frame.old },
      { file: `outlines/${fileName(frame.id)}.new.yml`, text: frame.now },
    ]),
  )
  writeFileSync(listFile, list)
  // The capture's checks, kept beside the outlines, so a rejudge of them shows them as this run made them.
  const captured: Checks = {
    built: source,
    before: [
      { name: 'every preset in baseline.json has a counterpart in the new app', problems: counterpartProblems(baseline.presets, presets) },
      { name: "every old frame is 1c's, hash for hash", problems: oldFrames },
      { name: 'the built file logs no error and asks for nothing but itself', problems: [...watched.errors, ...watched.requests] },
    ],
    after: [againCheck(previous, list)],
  }
  writeFileSync(join(reportDir, 'checks.json'), `${JSON.stringify(captured, null, 2)}\n`)
  return finish(prototype, baseline, frames, images, source, captured.before, captured.after)
}

/** A run's capture checks, kept in `.parity/report/checks.json` under the build they were made of: those judged before the verdicts' and those after. */
interface Checks {
  built: string
  before: Check[]
  after: Check[]
}

/** The notes the report opens with: the pairs reached otherwise, and the boards with no new side. */
const reportNotes = [
  `The old side of training:regimes is training, still, with its Regimes button clicked (a pressed-state button): the old prototype opens no preset on Regimes. The four settings presets 1i added are opened on the old side by the same start, through its applyStart's pane fall-through; neither kind has a picture in 1c's baseline, so theirs are shot here.`,
  `Exploration's five boards (Appearance, Chat, Inspector, Main, Trace) are not ported (1q's default), so they have no new side.`,
  `Bugs found in the old prototype, noted and not fixed (it is frozen, 1c): it ignores the apogee mode in the session list (its 13 rows at missing and config-typo), in Training (its canned run training at missing), in the Library (its agents, symphonies and server listed under the CLI's refusal at config-typo, and its editors opened at missing and config-typo) and in Review (its dry run and post sheet at missing). The new side draws what the CLI would give, each a fix its item records.`,
  `Carried from 1p and not in any frame: Resume on a Tasks row other than the recorded resume ends in the stand-in's words, and a cancel at a turn boundary drops the next round's start line; both are live, and 1p's record names them as the stand-in's limits.`,
]

/** The outlines diffed and judged, the checks gathered, and the report and its summary written. */
function finish(prototype: string, baseline: Baseline, frames: PairOutlines[], images: ReportMeta['images'], source: string, before: Check[], after: Check[], notes: string[] = []): Run {
  const reportDir = join(prototype, '.parity/report')
  const context = repoContext(prototype)
  const verdicts = readVerdicts(join(prototype, 'parity/verdicts.json')).verdicts
  const run = judge(comparePairs(frames), verdicts)
  run.checks.unshift({ name: 'every verdict stands (a class, a reason, a fix its item, a data verdict its fixture)', problems: verdictProblems(verdicts, context) }, ...before)
  run.checks.push(...after)
  writeFileSync(join(reportDir, 'index.html'), report(run, { revision: baseline.revision, built: source, images, records: recordsOf(context.milestones), notes: [...notes, ...reportNotes] }))
  writeFileSync(join(reportDir, 'summary.md'), summaryTable(run))
  return run
}

/**
 * `--rejudge`: the last run's outlines judged again against verdicts.json as
 * it is now, and the report written again, with nothing captured -- for
 * writing verdicts. The last run's capture checks (`checks.json`) stand as it
 * made them, so a failed one still fails. Refused when the built file is not
 * the one they were read from, or the run left no checks.
 */
export function rejudge(options: ParityOptions): Run {
  const { prototype } = options
  const baseline = JSON.parse(readFileSync(join(prototype, 'parity/baseline.json'), 'utf8')) as Baseline
  const reportDir = join(prototype, '.parity/report')
  const listFile = join(reportDir, 'outlines.txt')
  if (!existsSync(listFile)) throw new Refusal('there is no last run to judge again: run npm run parity')
  const source = builtHash(join(prototype, '../main.html'))
  const read = /^# built (.*)$/m.exec(readFileSync(listFile, 'utf8'))?.[1]
  if (read !== source) throw new Refusal(`the last run read the build ${read}, and lib/design/main.html is now ${source}: run npm run parity`)
  const checksFile = join(reportDir, 'checks.json')
  if (!existsSync(checksFile)) throw new Refusal('the last run left no checks (.parity/report/checks.json): run npm run parity')
  const kept = JSON.parse(readFileSync(checksFile, 'utf8')) as Checks
  if (kept.built !== source) throw new Refusal(`the last run's checks are for the build ${kept.built}, and lib/design/main.html is now ${source}: run npm run parity`)
  const frames: PairOutlines[] = []
  const images: ReportMeta['images'] = {}
  for (const pair of pairsOf(baseline, newPresets())) {
    const stem = fileName(pair.id)
    const outline = (side: string) => readFileSync(join(reportDir, 'outlines', `${stem}.${side}.yml`), 'utf8')
    frames.push({ id: pair.id, ...(pair.preset ? { preset: pair.preset } : {}), ...(pair.mode ? { mode: pair.mode } : {}), old: outline('old'), now: outline('new') })
    images[pair.id] = { old: pair.old.image !== undefined ? `old/${pair.old.image}` : `old/screens/${stem}.png`, now: `new/${pair.kind === 'screen' ? 'screens' : 'boards'}/${stem}.png` }
  }
  return finish(prototype, baseline, frames, images, source, kept.before, kept.after, ['Judged again (--rejudge) from the last run’s outlines: its frames, and its checks of them, stand as that run made them.'])
}

if (import.meta.main) {
  const prototype = resolve(dirname(fileURLToPath(import.meta.url)), '..')
  try {
    const repo = execFileSync('git', ['--no-optional-locks', '-C', prototype, 'rev-parse', '--show-toplevel'], { encoding: 'utf8' }).trim()
    const options = { repo, prototype, log: (line: string) => console.log(line) }
    const run = process.argv.includes('--rejudge') ? rejudge(options) : await parity(options)
    const problems = run.checks.flatMap((check) => check.problems)
    const total = run.pairs.reduce((sum, pair) => sum + pair.judged.length, 0)
    console.log(`${run.pairs.length} pairs, ${total} differences: the report is ${join(prototype, '.parity/report/index.html')}`)
    for (const check of run.checks) {
      if (check.problems.length) console.log(`FAIL ${check.name} (${check.problems.length})`)
      else if (check.skipped !== undefined) console.log(`skip ${check.name}: ${check.skipped}`)
      else console.log(`ok   ${check.name}`)
    }
    if (problems.length) {
      for (const problem of problems.slice(0, 200)) console.error(problem)
      if (problems.length > 200) console.error(`… and ${problems.length - 200} more (the report lists them all)`)
      process.exitCode = 1
    }
  } catch (error) {
    if (!(error instanceof Refusal)) throw error
    console.error(`npm run parity: ${error.message}`)
    process.exitCode = 1
  }
}
