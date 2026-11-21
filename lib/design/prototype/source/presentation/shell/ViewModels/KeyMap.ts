import { destinations, titleOf } from './Destination'
import type { Destination } from './Destination'

/** A modifier key, in the order the Mac's menus write them: ⌃⌥⇧⌘. */
export type Modifier = 'control' | 'option' | 'shift' | 'command'

const modifierOrder: readonly Modifier[] = ['control', 'option', 'shift', 'command']
const modifierSymbols: Readonly<Record<Modifier, string>> = { control: '⌃', option: '⌥', shift: '⇧', command: '⌘' }

/** One key with its modifiers, `⌥⌘M`: the Mac app's `KeyChord`. The key is one character, lower case for a letter. */
export interface KeyChord {
  readonly key: string
  readonly modifiers: readonly Modifier[]
}

/** A chord, its modifiers in their order. */
export function chord(key: string, ...modifiers: Modifier[]): KeyChord {
  return { key, modifiers: modifierOrder.filter((modifier) => modifiers.includes(modifier)) }
}

/** The chord as the Mac's menus write it: `⌃⌥⇧⌘X`. */
export function symbols(keys: KeyChord): string {
  return modifierOrder.filter((modifier) => keys.modifiers.includes(modifier)).map((modifier) => modifierSymbols[modifier]).join('') + keys.key.toUpperCase()
}

/** What the window does at a key, wherever its focus is: the Mac app's `ShellCommand`. */
export type ShellCommand =
  | { readonly kind: 'go'; readonly destination: Destination }
  | { readonly kind: 'modelsLibrary' }
  | { readonly kind: 'toggleMonitor' }
  | { readonly kind: 'toggleConsole' }
  | { readonly kind: 'toggleSidebar' }
  | { readonly kind: 'openSettings' }

/** A key something binds, and its words: the shell's, or a place's (Sessions' ⌘N …), as Settings › Keyboard lists it (design 1i). */
export interface BoundKey {
  readonly chord: KeyChord
  readonly label: string
}

/** A command, its key and its words. */
export interface KeyBinding extends BoundKey {
  readonly command: ShellCommand
}

/** One line of the design's keyboard list (Settings › Keyboard). */
export interface KeyReference {
  readonly label: string
  readonly keys: string
}

/** ⌘1 for the first place, ⌘9 for the ninth, ⌘0 for the tenth; the eleventh, Review, ⇧⌘R. */
export function keyOf(destination: Destination): KeyChord {
  if (destination === 'review') return chord('r', 'shift', 'command')
  const number = destinations.indexOf(destination) + 1
  return chord(number === 10 ? '0' : String(number), 'command')
}

/**
 * The keys, as the design gives them (`main.html`'s `set.keys` at `b375fac9`,
 * the old BuildLayout board's keys): the Mac app's `KeyMap`, as data. Settings ›
 * Keyboard (1i) lists `reference`, and `keyboardLines` adds any bound key
 * no line names; the shell answers `shell`.
 */
export const KeyMap = {
  /** The window's own keys: every place, and every overlay the shell opens. A screen's own keys arrive with it. */
  shell: [
    ...destinations.map((destination): KeyBinding => ({ command: { kind: 'go', destination }, chord: keyOf(destination), label: titleOf(destination) })),
    { command: { kind: 'modelsLibrary' }, chord: chord('l', 'shift', 'command'), label: 'Models Library' },
    { command: { kind: 'toggleMonitor' }, chord: chord('m', 'option', 'command'), label: 'System Monitor' },
    { command: { kind: 'toggleConsole' }, chord: chord('k', 'shift', 'command'), label: 'Console' },
    { command: { kind: 'toggleSidebar' }, chord: chord('s', 'control', 'command'), label: 'Hide Sidebar' },
    { command: { kind: 'openSettings' }, chord: chord(',', 'command'), label: 'Settings' },
  ] as readonly KeyBinding[],

  /** The design's whole keyboard list, in its order and words. */
  reference: [
    ['New session', '⌘N'],
    ['New private session', '⇧⌘N'],
    ['New execute session', '⌥⌘N'],
    ['Switch model', '⇧⌘M'],
    ['Show or hide the inspector', '⌥⌘I'],
    ['System monitor', '⌥⌘M'],
    ['Console', '⇧⌘K'],
    ['Settings', '⌘,'],
    ['Stop the answer', '⌘.'],
    ['Search sessions', '⇧⌘F'],
    ['Sessions, Models, Suites, Tasks, Code graph, Video, Voice, Knowledge, Training, Library', '⌘1 – ⌘9, ⌘0'],
    ['Review', '⇧⌘R'],
    ['Models library', '⇧⌘L'],
    ['Send', '⏎'],
    ['New line', '⇧⏎'],
    ['Commands', '/'],
    ['Mention a file', '@'],
    ['Allow once on a permission card', '⏎'],
    ['Deny on a permission card', 'esc'],
    ['Pick an answer on a card', '1 – 4'],
    ['Play or pause the focused player', 'space'],
    ['Generate, Speak, Plan or Render, from its form', '⌘⏎'],
    ['Hide the sidebar', '⌃⌘S'],
  ].map(([label, keys]): KeyReference => ({ label: label ?? '', keys: keys ?? '' })) as readonly KeyReference[],
}

/**
 * The chords a keyboard line names, as `symbols` writes them: `⌘N`; a list,
 * `⌘1 – ⌘9, ⌘0`; a range of digits under one set of modifiers, `⌘1 – ⌘9`
 * or `1 – 4`, each digit in it.
 */
export function chordsNamed(keys: string): string[] {
  return keys.split(', ').flatMap((part) => {
    const range = /^(\D*)(\d) – (\D*)(\d)$/.exec(part)
    if (!range || range[1] !== range[3]) return [part]
    const from = Number(range[2])
    const to = Number(range[4])
    return Array.from({ length: to - from + 1 }, (_, index) => `${range[1] ?? ''}${from + index}`)
  })
}

/**
 * The keyboard list Settings › Keyboard draws (design 1i): the design's
 * lines, then a line in its own words for each bound key no line names, so
 * a key added to the map shows there with no edit to Settings.
 */
export function keyboardLines(reference: readonly KeyReference[], bound: readonly BoundKey[]): KeyReference[] {
  const named = new Set(reference.flatMap((line) => chordsNamed(line.keys)))
  const added: KeyReference[] = []
  for (const key of bound) {
    const keys = symbols(key.chord)
    if (named.has(keys)) continue
    named.add(keys)
    added.push({ label: key.label, keys })
  }
  return [...reference, ...added]
}

/** What a browser's key event says, as far as a chord needs it. */
export interface KeyPress {
  /** Absent on the plain keydown Chrome's autofill dispatches: such a press is no chord the shell owns. */
  readonly key?: string
  /** The physical key: `KeyM`, `Digit1`, `Comma`. Option changes `key` (⌥M types µ), never `code`. */
  readonly code?: string
  readonly metaKey: boolean
  readonly ctrlKey: boolean
  readonly altKey: boolean
  readonly shiftKey: boolean
}

const codeKeys: Readonly<Record<string, string>> = { Comma: ',', Period: '.', Slash: '/', Enter: '⏎', Space: 'space', Escape: 'esc' }

/** The chord a key event is: its physical key, so ⌥⌘M is M whatever Option types. */
export function chordOf(event: KeyPress): KeyChord {
  const code = event.code ?? ''
  const key = /^Key[A-Z]$/.test(code)
    ? code.slice(3).toLowerCase()
    : /^Digit\d$/.test(code)
      ? code.slice(5)
      : (codeKeys[code] ?? (event.key ?? '').toLowerCase())
  const modifiers: Modifier[] = []
  if (event.ctrlKey) modifiers.push('control')
  if (event.altKey) modifiers.push('option')
  if (event.shiftKey) modifiers.push('shift')
  if (event.metaKey) modifiers.push('command')
  return chord(key, ...modifiers)
}

/** The shell command `keys` runs, if the shell owns it. */
export function commandFor(keys: KeyChord): ShellCommand | undefined {
  const wanted = symbols(keys)
  return KeyMap.shell.find((binding) => symbols(binding.chord) === wanted)?.command
}
