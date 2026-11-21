import { describe, expect, test } from 'vitest'
import { destinations, placeholderOf, titleOf } from '../../../source/presentation/shell/ViewModels/Destination'
import { chord, chordOf, chordsNamed, commandFor, keyboardLines, keyOf, KeyMap, symbols } from '../../../source/presentation/shell/ViewModels/KeyMap'
import { SessionKeys } from '../../../source/presentation/sessions/ViewModels/SessionKeys'
import { frozenComponent } from '../styles/Frozen'

describe('the places and their keys, as Destination.swift and KeyMap.swift hold them', () => {
  test("the design's eleven, in its order and names, on ⌘1 to ⌘9, ⌘0 and ⇧⌘R", () => {
    expect(destinations).toEqual(['sessions', 'models', 'suites', 'tasks', 'graph', 'video', 'voice', 'knowledge', 'training', 'library', 'review'])
    expect(destinations.map(titleOf)).toEqual(['Sessions', 'Models', 'Suites', 'Tasks', 'Code graph', 'Video', 'Voice', 'Knowledge', 'Training', 'Library', 'Review'])
    expect(destinations.map((destination) => symbols(keyOf(destination)))).toEqual(['⌘1', '⌘2', '⌘3', '⌘4', '⌘5', '⌘6', '⌘7', '⌘8', '⌘9', '⌘0', '⇧⌘R'])
  })

  test('every place and overlay the shell owns has a key, and no two share one', () => {
    const chords = KeyMap.shell.map((binding) => symbols(binding.chord))
    expect(new Set(chords).size).toBe(chords.length)
    const of = (kind: string) => KeyMap.shell.find((binding) => binding.command.kind === kind)
    expect(symbols(of('modelsLibrary')?.chord ?? keyOf('sessions'))).toBe('⇧⌘L')
    expect(symbols(of('toggleMonitor')?.chord ?? keyOf('sessions'))).toBe('⌥⌘M')
    expect(symbols(of('toggleConsole')?.chord ?? keyOf('sessions'))).toBe('⇧⌘K')
    expect(symbols(of('toggleSidebar')?.chord ?? keyOf('sessions'))).toBe('⌃⌘S')
    expect(symbols(of('openSettings')?.chord ?? keyOf('sessions'))).toBe('⌘,')
  })

  test("the design's keyboard list as data: 23 lines, in its order and words, each shell key among them", () => {
    expect(KeyMap.reference).toHaveLength(23)
    expect(KeyMap.reference[0]).toEqual({ label: 'New session', keys: '⌘N' })
    expect(KeyMap.reference).toContainEqual({ label: 'Sessions, Models, Suites, Tasks, Code graph, Video, Voice, Knowledge, Training, Library', keys: '⌘1 – ⌘9, ⌘0' })
    expect(KeyMap.reference.at(-1)).toEqual({ label: 'Hide the sidebar', keys: '⌃⌘S' })
    const listed = new Set(KeyMap.reference.map((line) => line.keys))
    for (const binding of KeyMap.shell) {
      if (binding.command.kind !== 'go') expect(listed.has(symbols(binding.chord)), binding.label).toBe(true)
    }
  })

  test("chords are written as the Mac's menus write them: ⌃⌥⇧⌘", () => {
    expect(symbols({ key: 'x', modifiers: ['command', 'shift', 'option', 'control'] })).toBe('⌃⌥⇧⌘X')
    expect(symbols({ key: ',', modifiers: ['command'] })).toBe('⌘,')
  })

  test('a key the browser passes is read by its physical key, so ⌥⌘M is M though Option types µ', () => {
    expect(chordOf({ key: 'µ', code: 'KeyM', metaKey: true, altKey: true, shiftKey: false, ctrlKey: false })).toEqual({ key: 'm', modifiers: ['option', 'command'] })
    expect(chordOf({ key: '!', code: 'Digit1', metaKey: true, altKey: false, shiftKey: true, ctrlKey: false })).toEqual({ key: '1', modifiers: ['shift', 'command'] })
    expect(chordOf({ key: ',', code: 'Comma', metaKey: true, altKey: false, shiftKey: false, ctrlKey: false })).toEqual({ key: ',', modifiers: ['command'] })
    expect(commandFor(chordOf({ key: 'µ', code: 'KeyM', metaKey: true, altKey: true, shiftKey: false, ctrlKey: false }))).toEqual({ kind: 'toggleMonitor' })
    expect(commandFor(chordOf({ key: '0', code: 'Digit0', metaKey: true, altKey: false, shiftKey: false, ctrlKey: false }))).toEqual({ kind: 'go', destination: 'library' })
    expect(commandFor(chordOf({ key: 'R', code: 'KeyR', metaKey: true, altKey: false, shiftKey: true, ctrlKey: false }))).toEqual({ kind: 'go', destination: 'review' })
    expect(commandFor(chordOf({ key: 'r', code: 'KeyR', metaKey: true, altKey: false, shiftKey: false, ctrlKey: false }))).toBeUndefined()
    expect(commandFor(chordOf({ key: 'm', code: 'KeyM', metaKey: false, altKey: false, shiftKey: false, ctrlKey: false }))).toBeUndefined()
  })

  test("a keydown with no key and no code, as Chrome's autofill dispatches, is no chord the shell owns, and does not throw", () => {
    expect(() => chordOf({ metaKey: false, ctrlKey: false, altKey: false, shiftKey: false })).not.toThrow()
    expect(commandFor(chordOf({ metaKey: false, ctrlKey: false, altKey: false, shiftKey: false }))).toBeUndefined()
  })
})

describe('the keyboard list Settings › Keyboard draws (design 1i)', () => {
  test('a line names its chords: one, a range of digits with its modifiers, or a list', () => {
    expect(chordsNamed('⌘N')).toEqual(['⌘N'])
    expect(chordsNamed('⌘,')).toEqual(['⌘,'])
    expect(chordsNamed('⌘1 – ⌘9, ⌘0')).toEqual(['⌘1', '⌘2', '⌘3', '⌘4', '⌘5', '⌘6', '⌘7', '⌘8', '⌘9', '⌘0'])
    expect(chordsNamed('1 – 4')).toEqual(['1', '2', '3', '4'])
  })

  test("checked once against the frozen prototype: its 23 rows are the map's lines, in its order and words", () => {
    const old = (frozenComponent({ start: 'settings:keyboard', still: true }) as unknown as { renderVals(): { set: { keys: Array<{ label: string; keys: string }> } } }).renderVals().set.keys
    expect(old).toHaveLength(23)
    expect(KeyMap.reference.map((line) => ({ label: line.label, keys: line.keys }))).toEqual(old)
  })

  test("every key the shell binds, and every key the places bind (Sessions' five), is named by a line: nothing is added", () => {
    const bound = [...KeyMap.shell, ...SessionKeys.bindings]
    const named = new Set(KeyMap.reference.flatMap((line) => chordsNamed(line.keys)))
    for (const binding of bound) expect(named.has(symbols(binding.chord)), binding.label).toBe(true)
    expect(keyboardLines(KeyMap.reference, bound)).toEqual(KeyMap.reference)
  })

  test('a key bound that no line names gets its own line, in its words, after the design\'s', () => {
    const planted = [...KeyMap.shell, { chord: chord('p', 'shift', 'command'), label: 'Print the transcript' }]
    expect(keyboardLines(KeyMap.reference, planted).slice(-2)).toEqual([KeyMap.reference.at(-1), { label: 'Print the transcript', keys: '⇧⌘P' }])
  })

  test("Sessions' keys carry the keyboard list's words", () => {
    expect(SessionKeys.bindings.map((binding) => binding.label)).toEqual(['New session', 'New private session', 'New execute session', 'Show or hide the inspector', 'Search sessions'])
  })
})

describe('a place not yet ported', () => {
  test('names the place and the design item that ports it', () => {
    expect(destinations.map((destination) => placeholderOf(destination)?.item)).toEqual([undefined, undefined, '1p', '1p', '1o', undefined, undefined, '1o', undefined, undefined, undefined])
    expect(placeholderOf('library'), 'Library is its place (1n): no placeholder').toBeUndefined()
    expect(placeholderOf('review'), 'Review is its place (1n): no placeholder').toBeUndefined()
    expect(placeholderOf('voice'), 'Voice is its place (1m): no placeholder').toBeUndefined()
    expect(placeholderOf('training'), 'Training is its place (1j): no placeholder').toBeUndefined()
    expect(placeholderOf('sessions'), 'Sessions is the session window (1u): no placeholder').toBeUndefined()
    expect(placeholderOf('models'), 'Models is its place (1h): no placeholder').toBeUndefined()
    expect(placeholderOf('video'), 'Video is its place (1k): no placeholder').toBeUndefined()
    expect(placeholderOf('suites')).toEqual({ title: 'Suites', item: '1p', says: 'Suites isn’t drawn here yet. Design item 1p ports it.' })
  })
})
