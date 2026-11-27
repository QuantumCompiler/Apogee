import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import type { Timers } from '../../../source/infrastructure/clock/Clock'
import { frameApp } from '../../../source/presentation/app/ViewModels/FrameApp'
import { galleryKey, galleryKeyClashes, opensGallery, WindowChoice, windowShows } from '../../../source/presentation/app/ViewModels/GalleryKey'
import { AppViewModel, windowKeys } from '../../../source/presentation/app/ViewModels/AppViewModel'
import { ApogeeApp, galleryParts } from '../../../source/presentation/app/Views/ApogeeApp'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { boards } from '../../../source/presentation/gallery/Models/Boards'
import { GalleryViewModel } from '../../../source/presentation/gallery/ViewModels/GalleryViewModel'
import { chord, KeyMap, keyboardLines, symbols } from '../../../source/presentation/shell/ViewModels/KeyMap'

/** Timers a test runs by hand: what a still frame never asks for, and a live one does. */
function handTimers(): Timers & { readonly pending: number; run(milliseconds: number): void } {
  const intervals = new Map<number, { callback: () => void; every: number; due: number }>()
  let now = 0
  let next = 1
  return {
    get pending() {
      return intervals.size
    },
    setInterval(callback, milliseconds) {
      intervals.set(next, { callback, every: milliseconds, due: now + milliseconds })
      return next++
    },
    clearInterval(handle) {
      intervals.delete(handle as number)
    },
    run(milliseconds) {
      const end = now + milliseconds
      for (;;) {
        const due = [...intervals.values()].sort((a, b) => a.due - b.due)[0]
        if (!due || due.due > end) break
        now = due.due
        due.due += due.every
        due.callback()
      }
      now = end
    },
  }
}

const flush = () => new Promise((resolve) => setTimeout(resolve, 0))
const gallery = () => new GalleryViewModel({ search: '?gallery', replace: () => undefined })
const framed = (app: AppViewModel) => renderToStaticMarkup(<ApogeeApp model={app} frame={{ width: 1440, height: 900, keys: false }} />)

describe("the gallery's key (design 1q): one key app owns, outside the shell's key map", () => {
  test('⌥⌘G, read by its physical key, whatever Option types; not ⌘G or ⇧⌘G, which the browser keeps for find', () => {
    expect(symbols(galleryKey)).toBe('⌥⌘G')
    expect(opensGallery({ key: '©', code: 'KeyG', altKey: true, metaKey: true, ctrlKey: false, shiftKey: false })).toBe(true)
    expect(opensGallery({ key: 'g', code: 'KeyG', altKey: false, metaKey: true, ctrlKey: false, shiftKey: false })).toBe(false)
    expect(opensGallery({ key: 'G', code: 'KeyG', altKey: false, metaKey: true, ctrlKey: false, shiftKey: true })).toBe(false)
  })

  test('nothing the app binds is the gallery’s key: the shell’s keys and the places’', () => {
    const app = new AppViewModel(presetFor({ still: true, apogee: 'installed' }).preset, modeRules)
    expect(galleryKeyClashes(app.boundKeys)).toEqual([])
  })

  test('a planted shell binding of the gallery’s key fails, naming it', () => {
    const planted = [...KeyMap.shell, { command: { kind: 'toggleSidebar' as const }, chord: chord('g', 'option', 'command'), label: 'Planted' }]
    expect(galleryKeyClashes(planted)).toEqual(["the app binds ⌥⌘G to Planted, and ⌥⌘G is the gallery's: the gallery opens outside the app's key map"])
  })

  test('it never shows in Settings › Keyboard: no line names it', () => {
    const app = new AppViewModel(presetFor({ still: true, apogee: 'installed' }).preset, modeRules)
    expect(keyboardLines(KeyMap.reference, app.boundKeys).filter((line) => line.keys.includes('⌥⌘G'))).toEqual([])
  })

  test("the gallery is handed the key map the layout page draws (1r): Settings › Keyboard's lines, from every key the window binds", () => {
    const app = new AppViewModel(presetFor({ still: true, apogee: 'installed' }).preset, modeRules)
    expect(app.boundKeys).toBe(windowKeys)
    expect(galleryParts([]).keys).toEqual(keyboardLines(KeyMap.reference, app.boundKeys))
    expect(galleryParts([]).keys).toHaveLength(23)
  })
})

describe('what the window shows: the app, or the gallery on ?gallery', () => {
  test('the gallery on ?gallery, ?gallery=foundations and &board=<id>; the app otherwise, ?page= among it', () => {
    expect(windowShows('?gallery')).toBe('gallery')
    expect(windowShows('?gallery=foundations&board=ColorTokens')).toBe('gallery')
    expect(windowShows('')).toBe('app')
    expect(windowShows('?start=models&still=true')).toBe('app')
    expect(windowShows('?page=foundations')).toBe('app')
  })
})

describe('the frames app mounts for the gallery (design 1q)', () => {
  test('each frame is its own app at its props: the preset, still, the theme and the apogee mode', async () => {
    const view = gallery()
    view.setTheme('ScreenModels', 'light')
    view.setApogee('ScreenModels', 'missing')
    const app = frameApp(view.frame('ScreenModels')!)
    expect([app.preset.route, app.clock.still, app.preset.apogee, app.theme.theme]).toEqual(['models', true, 'missing', 'light'])
    await app.start()
    await flush()
    expect(framed(app)).toContain('data-theme="light"')
    app.stop()
  })

  test('the App board runs live, at the app’s own start', async () => {
    const timers = handTimers()
    const app = frameApp(gallery().frame('App')!, timers)
    expect([app.preset.route, app.clock.still]).toEqual(['chat', false])
    await app.start()
    expect(timers.pending).toBe(1)
    const before = app.clock.ticks
    timers.run(400 * 3)
    expect(app.clock.ticks).toBe(before + 3)
    app.stop()
  })

  for (const entry of boards.filter((each) => each.draws === 'screen')) {
    test(`${entry.id} (${entry.start}): still -- the same after 30 ticks as at its first, no throw, no undefined or NaN`, async () => {
      const timers = handTimers()
      const app = frameApp(gallery().frame(entry.id)!, timers)
      await app.start()
      await flush()
      const ticks = app.clock.ticks
      const first = framed(app)
      timers.run(400 * 30)
      await flush()
      expect(timers.pending).toBe(0)
      expect(app.clock.ticks).toBe(ticks)
      expect(framed(app)).toBe(first)
      expect(first).not.toMatch(/\bundefined\b|\bNaN\b/)
      app.stop()
    })
  }
})

describe("the gallery's key turns the window between the app and the gallery, the address with it", () => {
  test('from the app the gallery, its words added to the address; from the gallery the app, its words taken out', () => {
    const written: string[] = []
    const address = { search: '?start=models&still=true', replace: (search: string) => void written.push(search) }
    const choice = new WindowChoice(address)
    expect(choice.shows).toBe('app')
    choice.toggle()
    expect([choice.shows, written.at(-1)]).toEqual(['gallery', '?start=models&still=true&gallery'])
    const opened = new WindowChoice({ search: '?gallery=foundations&board=Logo', replace: (search: string) => void written.push(search) })
    expect(opened.shows).toBe('gallery')
    opened.toggle()
    expect([opened.shows, written.at(-1)]).toEqual(['app', ''])
  })
})
