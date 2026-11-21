import { describe, expect, test } from 'vitest'
import { processBoxes } from '../../../source/presentation/gallery/Models/processes'
import { BuildProcessesViewModel } from '../../../source/presentation/gallery/ViewModels/BuildProcessesViewModel'
import { colorNames } from '../../../source/presentation/styles/Tokens/Tokens'

const face = () => new BuildProcessesViewModel().face()

describe('the processes page (design 1r): the app, its apogee children and the files it reads and writes', () => {
  test("the board's 1440 × 1040 page, its title and words", () => {
    const page = face()
    expect([page.width, page.height]).toEqual([1440, 1040])
    expect(page.title).toBe('How the app runs Apogee')
    expect(page.intro).toBe('One app process starting `apogee` children and speaking to them over their pipes. It never links Apogee and never opens a port: only `apogee serve` owns one, and that is for server deployments.')
  })

  test("the nine boxes where the board drew them, each in its words, the app's own in the accent and still \"SwiftUI first\"", () => {
    expect(face().boxes.map((box) => [box.id, box.x, box.y, box.width, box.height, box.title, box.border])).toEqual([
      ['files', 64, 216, 300, 120, 'The app’s own files', 'line.strong'],
      ['app', 64, 400, 300, 200, 'Apogee.app', 'status.accent'],
      ['records', 64, 664, 300, 136, 'Records beside the media', 'line.strong'],
      ['session', 700, 168, 676, 92, 'Each open session: one chat child, alive while it is in view or mid-turn', 'line.strong'],
      ['task', 700, 308, 676, 92, 'Each task the app starts: one task run child', 'line.strong'],
      ['media', 700, 448, 676, 92, 'Each clip, composition or speech: one run', 'line.strong'],
      ['reads', 700, 588, 676, 92, 'Reads: one-shot, run again after every change the app makes', 'line.strong'],
      ['changes', 700, 728, 676, 92, 'Changes: one command each, the one a person would type', 'line.strong'],
      ['machine', 700, 868, 676, 92, 'The machine, while the system monitor is open', 'line.strong'],
    ])
    expect(processBoxes.find((box) => box.id === 'app')?.lines).toEqual([
      { kind: 'note', text: 'SwiftUI first' },
      { kind: 'space' },
      { kind: 'plain', text: 'one process supervisor' },
      { kind: 'plain', text: 'a protocol client generated from `machine-schema.json`' },
      { kind: 'plain', text: 'stores re-read from the CLI' },
    ])
    expect(processBoxes.find((box) => box.id === 'session')?.lines).toEqual([
      { kind: 'code', text: 'apogee chat --output-format stream-json --input-format stream-json --resume ‹id›' },
      { kind: 'note', text: 'in: user, answer, attach, cancel · out: the turn’s events, a question, then result or error' },
    ])
  })

  test('the connectors: writes and reads dashed between the stacked boxes, and starts, over pipes fanning out to the six children, an arrow to each one’s middle', () => {
    const page = face()
    expect(page.lines.map((line) => [line.x1, line.y1, line.x2, line.y2, line.dashed, line.arrow])).toEqual([
      [214, 336, 214, 400, true, false],
      [214, 600, 214, 664, true, false],
      [364, 500, 532, 500, false, false],
      [532, 214, 532, 914, false, false],
      [532, 214, 699, 214, false, true],
      [532, 354, 699, 354, false, true],
      [532, 494, 699, 494, false, true],
      [532, 634, 699, 634, false, true],
      [532, 774, 699, 774, false, true],
      [532, 914, 699, 914, false, true],
    ])
    // A link's label beside its line, at its middle; the fan's over its first stretch, centred between the app and the bus.
    expect(page.labels.map((label) => [label.text, label.x, label.y, label.width ?? null])).toEqual([
      ['writes', 226, 360, null],
      ['reads', 226, 624, null],
      ['starts, over pipes', 380, 472, 136],
    ])
  })

  test('moving a child box moves its arrow and stretches the bus to it', () => {
    const moved = processBoxes.map((box) => (box.id === 'machine' ? { ...box, y: 1008 } : box))
    const page = new BuildProcessesViewModel(moved).face()
    expect(page.lines.at(-1)).toMatchObject({ x1: 532, y1: 1054, x2: 699, y2: 1054 })
    expect(page.lines[3]).toMatchObject({ y1: 214, y2: 1054 })
  })

  test('every color the page draws is a token, never a hex: it follows the theme', () => {
    const page = face()
    const colors = [...page.boxes.flatMap((box) => [box.border, box.fill]), page.background, page.ink, page.note, page.connector]
    for (const color of colors) expect(colorNames, color).toContain(color)
    expect(new Set(colors)).toEqual(new Set(['line.strong', 'status.accent', 'surface.card', 'surface.window', 'text.primary', 'text.secondary', 'text.tertiary']))
  })
})
