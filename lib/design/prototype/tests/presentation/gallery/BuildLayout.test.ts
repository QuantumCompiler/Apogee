import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { windowKeys } from '../../../source/presentation/app/ViewModels/AppViewModel'
import { keyRows } from '../../../source/presentation/gallery/Models/layout'
import { nativeControls, nativeControlsHead } from '../../../source/presentation/gallery/Models/nativeControls'
import { BuildLayoutViewModel, layoutTokens } from '../../../source/presentation/gallery/ViewModels/BuildLayoutViewModel'
import type { BuildLayoutFace, KeyLine, LayoutTokens } from '../../../source/presentation/gallery/ViewModels/BuildLayoutViewModel'
import { chord, keyboardLines, KeyMap } from '../../../source/presentation/shell/ViewModels/KeyMap'
import { tokensFile } from '../../../tooling/tokens'

/** The prototype's root: this file is tests/presentation/gallery/BuildLayout.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')

/** The key map `app` hands the gallery: Settings › Keyboard's lines. */
const keyMap: readonly KeyLine[] = keyboardLines(KeyMap.reference, windowKeys)

const layout = (tokens: LayoutTokens = layoutTokens(), keys: readonly KeyLine[] = keyMap) => new BuildLayoutViewModel(keys, tokens)

/** tokens.json read from disk, as the build reads it (`tokensFile`, whose root 1t's move changed). */
const tokensJson = JSON.parse(readFileSync(tokensFile(root), 'utf8')) as { layoutPx: Record<string, number | Record<string, number>> }

/**
 * Every text the page shows that may carry a size: its words, the window's
 * labels, the regions, the key rows' words (never their keys, which the key
 * map draws), the states, the type sizes and space, shape and motion. The
 * native controls are names, not sizes.
 */
function shown(face: BuildLayoutFace): string[] {
  return [
    ...Object.values(face.words),
    ...face.window.boxes.flatMap((box) => [box.title, box.note ?? '']),
    ...face.regions.flatMap((region) => [region.name, region.size, region.words]),
    ...face.keys.map((row) => row.words),
    ...face.states.flatMap((state) => [state.name, state.words]),
    ...face.type.map((size) => size.px),
    ...face.space.flatMap((row) => [row.name, row.words]),
  ]
}

/** The words that carry a number but are no size, as the board writes them: the board's place among the Build boards, a read's wait, and the contrast floor. */
const wordsWithNumbers = ['BUILD · 2 OF 2', '300 ms', '4.5:1']

/** The numbers a text shows: a digit run not part of a name (`h1`, `--t-h2`), whatever follows it (`480px`, `32.`). */
function numbersIn(text: string): number[] {
  const words = wordsWithNumbers.reduce((rest, word) => rest.replaceAll(word, ''), text)
  return [...words.matchAll(/(?<![\w.-])\d+(?:\.\d+)?/g)].map((match) => Number(match[0]))
}

/** Every token the page reads, planted at a value of its own, 9001 and up, so a number the page shows is either one of them or typed by hand. */
function planted(): { tokens: LayoutTokens; values: Set<number> } {
  const real = layoutTokens()
  let next = 9001
  const values = new Set<number>()
  const plant = () => {
    values.add(next)
    return next++
  }
  const tokens: LayoutTokens = {
    layout: real.layout.map((size) => ({ ...size, px: plant() })),
    spacing: real.spacing.map(() => plant()),
    controls: real.controls.map((size) => ({ ...size, px: plant() })),
    radii: real.radii.map((radius) => ({ ...radius, value: String(plant()) })),
    sizes: real.sizes.map((size) => ({ ...size, px: plant() })),
    motion: { chevron: `${plant()}ms ease`, pulse: `${plant()}s ease-in-out, opacity 1 to 0.3`, caret: `${plant()}s steps(1) blink` },
    scrim: 'rgba(0,0,0,0.37)',
  }
  values.add(37)
  return { tokens, values }
}

describe("the layout page's sizes (design 1r): every one from the tokens", () => {
  test("the regions and sizes in the board's words, each size read from tokens.json's layoutPx, the menus' two widths among them", () => {
    expect(layout().face().regions.map((region) => [region.name, region.size, region.words])).toEqual([
      ['Window', 'at least 960 × 640', 'Below 900 wide the sidebar becomes a 56 px column of icons, and side panes close.'],
      ['Sidebar', '256', 'Sessions by date, places, system, settings. Every group remembers whether it is open.'],
      ['Header', '52', 'The screen’s title and its own controls. In a session: model, thinking, context, inspector.'],
      ['Inspector', '360', 'Sessions only. Closing it gives the conversation the room.'],
      ['Detail panes', '380 · 360 · 340 · 300', 'A model’s details · generating a clip · a composition’s changes · a code symbol.'],
      ['List columns', '300 · 280 · 264 · 230', 'Tasks · suites · clips, compositions and speeches · the code graph’s files.'],
      ['Content widths', '860 · 820', 'The conversation, compositions and tasks · clips, voice and roles.'],
      ['Settings', '1080 × 780, nav 220', 'A sheet over the window; changes apply as they are made.'],
      ['Dialogs', '600 · 580 · 460', 'New task · download, install · confirm.'],
      ['Menus', '380 · 340', 'Thinking · switch model.'],
      ['Toast', 'bottom centre', 'One line, gone after about five seconds, never the only place something is said.'],
    ])
    expect(tokensJson.layoutPx['menu']).toEqual({ thinking: 380, switchModel: 340 })
  })

  test('space, shape and motion, and the type sizes at Medium, read from the tokens', () => {
    const face = layout().face()
    expect(face.space.map((row) => [row.name, row.words])).toEqual([
      ['Spacing', 'A 2 pt step: 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 32.'],
      ['Control heights', '24 small · 28 buttons, fields, rows · 32 header controls · 44 list rows · 52 headers.'],
      ['Radii', '3 tags · 4 chips · 5 rows · 6 controls · 7 segmented · 8 cards · 10 menus · 12 dialogs.'],
      ['Motion', 'Chevrons turn in 150 ms. Live things pulse at 1.4 s; the caret blinks at 1.1 s. With Reduce Motion both stop.'],
      ['Focus', 'A 2 pt ring in the accent, 1 pt out, on every control.'],
      ['Contrast', 'Text 4.5:1 against what it sits on, checked live in Settings › Appearance as colours change.'],
      ['Not tokens', 'The sheet’s scrim (black at 45%) and the menu and dialog shadows; window controls are the platform’s.'],
    ])
    expect(face.type.map((size) => [size.variable, size.sample, size.px])).toEqual([
      ['--t-display', 'Apogee display', '24 px'],
      ['--t-h1', 'Apogee h1', '19 px'],
      ['--t-h2', 'Apogee h2', '16 px'],
      ['--t-h3', 'Apogee h3', '14 px'],
      ['--t-title', 'Apogee title', '13.5 px'],
      ['--t-prose', 'Apogee prose', '14 px'],
      ['--t-body', 'Apogee body', '13 px'],
      ['--t-dense', 'Apogee dense', '12.5 px'],
      ['--t-small', 'Apogee small', '12 px'],
      ['--t-code', 'Apogee code', '12 px'],
      ['--t-ui', 'Apogee ui', '11.5 px'],
      ['--t-meta', 'Apogee meta', '11 px'],
      ['--t-label', 'Apogee label', '10.5 px'],
      ['--t-tag', 'Apogee tag', '9.5 px'],
    ])
  })

  test('every number the page shows comes from the tokens: with each token planted at a value of its own, the page shows those and nothing else', () => {
    const { tokens, values } = planted()
    const numbers = shown(layout(tokens).face()).flatMap(numbersIn)
    expect(numbers.length).toBeGreaterThan(40)
    expect(numbers.filter((number) => !values.has(number))).toEqual([])
    // And on the real tokens, every number shown is one of theirs.
    const real = layoutTokens()
    const held = new Set([...real.layout.map((size) => size.px), ...real.spacing, ...real.controls.map((size) => size.px), ...real.radii.map((radius) => Number(radius.value)), ...real.sizes.map((size) => size.px), 150, 1.4, 1.1, 45])
    expect(shown(layout().face()).flatMap(numbersIn).filter((number) => !held.has(number))).toEqual([])
  })

  test('a planted change to layoutPx.sidebar changes the Sidebar row and the window diagram', () => {
    const real = layoutTokens()
    const wider: LayoutTokens = { ...real, layout: real.layout.map((size) => (size.name === 'sidebar' ? { ...size, px: 300 } : size)) }
    const face = layout(wider).face()
    expect(face.regions.find((region) => region.name === 'Sidebar')?.size).toBe('300')
    const sidebar = face.window.boxes.find((box) => box.part === 'sidebar')
    expect([sidebar?.title, sidebar?.width]).toEqual(['Sidebar · 300', 225])
    expect(face.window.boxes.find((box) => box.part === 'header')?.x).toBe(225)
  })

  test('the window at three-quarter scale, drawn from the tokens: the window 1080 × 675, the sidebar 192, the header 39, the inspector 270, the old board’s boxes where it drew them', () => {
    const face = layout().face()
    expect(face.window.scale).toBe(0.75)
    expect([face.window.width, face.window.height]).toEqual([1080, 675])
    expect(face.window.boxes.map((box) => [box.part, box.x, box.y, box.width, box.height, box.title, box.note ?? ''])).toEqual([
      ['window', 0, 0, 1080, 675, '', ''],
      ['sidebar', 0, 0, 192, 675, 'Sidebar · 256', ''],
      ['sessions', 9, 48, 174, 390, 'Sessions', 'grouped by date, each group collapsible · search ⇧⌘F'],
      ['places', 9, 450, 174, 48, 'Places · 40 × 34 each', 'scrolls sideways · a dot when something runs'],
      ['system', 9, 510, 174, 90, 'System', 'collapsible · cpu, memory, gpu · opens the monitor ⌥⌘M'],
      ['settings', 9, 612, 174, 48, 'Settings ⌘,', 'and the theme'],
      ['header', 192, 0, 888, 39, 'Header · 52', 'title and the screen’s controls'],
      ['content', 192, 39, 618, 636, 'Content', 'fluid · the conversation at most 860 wide, centred'],
      ['inspector', 810, 39, 270, 636, 'Inspector · 360', 'Sessions only · ⌥⌘I · This turn, Context, Model, Helpers, each collapsible'],
      ['composer', 222, 540, 558, 105, 'Composer', 'attachments as chips · / commands · @ files · ⏎ sends, ⇧⏎ new line'],
    ])
  })
})

describe("the layout page's keys (design 1r): every one from the key map app hands it", () => {
  test("Navigation and keys: BuildLayout's 13 rows in its words, each row's keys drawn from the key map", () => {
    expect(layout().face().keys.map((row) => [row.words, row.keys])).toEqual([
      ['Sessions, Models, Suites, Tasks, Code graph, Video, Voice, Knowledge, Training, Library', '⌘1 – ⌘9, ⌘0'],
      ['Review, the eleventh place', '⇧⌘R'],
      ['New session · new private session · new execute session', '⌘N · ⇧⌘N · ⌥⌘N'],
      ['Search sessions', '⇧⌘F'],
      ['Switch model · models library', '⇧⌘M · ⇧⌘L'],
      ['Inspector · system monitor · settings', '⌥⌘I · ⌥⌘M · ⌘,'],
      ['The console: any command, as typed', '⇧⌘K'],
      ['Send · new line · commands · mention a file', '⏎ · ⇧⏎ · / · @'],
      ['Stop what is running here', '⌘.'],
      ['On a permission card: allow once · deny · pick', '⏎ · esc · 1 – 4'],
      ['Play or pause the focused player', 'space'],
      ['Generate, Speak, Plan or Render, from its form', '⌘⏎'],
      ['Hide the sidebar', '⌃⌘S'],
    ])
    expect(layout().face().words.keysIntro).toBe('Every destination and overlay has a key, and nothing needs the pointer. Space and ⌘⏎ are new in the media screens.')
  })

  test('every key-map entry appears in a key row: the real map leaves none out', () => {
    expect(keyMap).toHaveLength(23)
    expect(layout().keyProblems()).toEqual([])
    expect(new Set(keyRows.flatMap((row) => row.lines)).size).toBe(23)
  })

  test('a planted extra key in the map fails until a row names it', () => {
    const extra = keyboardLines(KeyMap.reference, [...windowKeys, { chord: chord('j', 'control', 'command'), label: 'Jump to the latest turn' }])
    expect(layout(layoutTokens(), extra).keyProblems()).toEqual(['the key map’s line "Jump to the latest turn" (⌃⌘J) is on no row of Navigation and keys'])
    const named = new BuildLayoutViewModel(extra, layoutTokens(), [...keyRows, { words: 'Jump to the latest turn', lines: ['Jump to the latest turn'] }])
    expect(named.keyProblems()).toEqual([])
    expect(named.face().keys.at(-1)).toEqual({ words: 'Jump to the latest turn', keys: '⌃⌘J' })
  })

  test('a row or a label naming a line the key map lacks fails, naming it once', () => {
    // "Search sessions" is the fourth row's, and the window's Sessions box names it too.
    const short = keyMap.filter((line) => line.label !== 'Search sessions')
    expect(layout(layoutTokens(), short).keyProblems()).toEqual(['the layout page names the key "Search sessions", which the key map does not hold'])
  })
})

/** The Xcode project's floor, read only: every MACOSX_DEPLOYMENT_TARGET it sets. */
const project = readFileSync(resolve(root, '../../src/darwin/Apogee.xcodeproj/project.pbxproj'), 'utf8')

/** Where a native-controls header's macOS and the Xcode project's deployment target disagree, each a sentence. */
function floorProblems(header: string, pbxproj: string): string[] {
  const named = /MACOS (\d+(?:\.\d+)?)/.exec(header)?.[1]
  const targets = [...pbxproj.matchAll(/MACOSX_DEPLOYMENT_TARGET = ([\d.]+);/g)].map((match) => match[1] ?? '')
  if (named === undefined) return [`the native controls' header "${header}" names no macOS`]
  if (targets.length === 0) return ['the Xcode project sets no MACOSX_DEPLOYMENT_TARGET']
  return targets.filter((target) => Number(target) !== Number(named)).map((target) => `the native controls' header names macOS ${named}, and the Xcode project's MACOSX_DEPLOYMENT_TARGET is ${target}`)
}

describe('the native controls (design 1r)', () => {
  test("the board's 16 rows, SwiftUI, Qt Quick and WinUI 3, under its headers", () => {
    expect(nativeControls).toHaveLength(16)
    expect(nativeControls[0]).toEqual({ design: 'Sidebar and content', swiftui: 'NavigationSplitView', qtQuick: 'SplitView and ListView', winui: 'NavigationView, left' })
    expect(layout().face().controls.rows).toEqual(nativeControls)
    expect(layout().face().controls.head).toEqual({ design: 'THE DESIGN', swiftui: 'SWIFTUI · MACOS 27', qtQuick: 'QT QUICK · LINUX', winui: 'WINUI 3 · WINDOWS' })
  })

  test("the header's macOS is the Xcode project's MACOSX_DEPLOYMENT_TARGET, 27 against 27.0 (the floor)", () => {
    expect([...project.matchAll(/MACOSX_DEPLOYMENT_TARGET = ([\d.]+);/g)].map((match) => match[1])).toEqual(['27.0', '27.0'])
    expect(floorProblems(layout().face().controls.head.swiftui, project)).toEqual([])
    expect(nativeControlsHead.swiftui).toBe('SWIFTUI · MACOS 27')
  })

  test('a planted 14 fails, as the old board said', () => {
    expect(floorProblems('SWIFTUI · MACOS 14', project)).toEqual([
      "the native controls' header names macOS 14, and the Xcode project's MACOSX_DEPLOYMENT_TARGET is 27.0",
      "the native controls' header names macOS 14, and the Xcode project's MACOSX_DEPLOYMENT_TARGET is 27.0",
    ])
  })
})

describe("the layout page's states (design 1r)", () => {
  test("the six states every screen draws, in the board's words", () => {
    expect(layout().face().states.map((state) => state.name)).toEqual(['Reading', 'Empty', 'A command failed', 'The CLI is missing or too old', 'Something is running', 'Cloud, billed'])
    expect(layout().face().states[0]?.words).toBe('The screen’s shape with quiet placeholders. No spinner for a read under 300 ms.')
  })
})

describe("the layout page's introduction (design 1t)", () => {
  test('it says the numbers are in lib/design/tokens.json, beside the built file, not in the retired canvas', () => {
    const intro = layout().face().words.intro
    expect(intro).toBe(
      'The measurements behind every screen, how a person moves between them, the states each one draws, and what each piece of the design becomes in SwiftUI first, then Qt Quick and WinUI. The numbers are the prototype’s own; with the tokens they are in `lib/design/tokens.json`.',
    )
    expect(intro).not.toContain('canvas')
  })
})
