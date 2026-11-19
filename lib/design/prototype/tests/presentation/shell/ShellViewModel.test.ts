import { describe, expect, test } from 'vitest'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { ThemeService } from '../../../source/presentation/styles/Appearance/ThemeService'
import { destinations } from '../../../source/presentation/shell/ViewModels/Destination'
import { chordOf, keyOf, symbols } from '../../../source/presentation/shell/ViewModels/KeyMap'
import { ShellViewModel } from '../../../source/presentation/shell/ViewModels/ShellViewModel'

/** A shell over a clock the test drives. */
function shell() {
  const clock = new Clock({ still: true })
  const theme = new ThemeService()
  return { clock, theme, shell: new ShellViewModel(clock, theme) }
}

/** The key event a browser passes for `symbols`, e.g. `⇧⌘R`. */
function press(text: string) {
  const key = text.replace(/[⌃⌥⇧⌘]/g, '')
  const code = /^\d$/.test(key) ? `Digit${key}` : key === ',' ? 'Comma' : `Key${key.toUpperCase()}`
  return chordOf({ key, code, metaKey: text.includes('⌘'), shiftKey: text.includes('⇧'), altKey: text.includes('⌥'), ctrlKey: text.includes('⌃') })
}

describe('the shell view-model', () => {
  test('it opens at Sessions, the sidebar shown, nothing over the window', () => {
    const { shell: model } = shell()
    expect(model.route).toBe('sessions')
    expect(model.overlay).toBeUndefined()
    expect(model.sidebarHidden).toBe(false)
    expect(model.toast).toBeUndefined()
    expect(model.systemOpen).toBe(false)
  })

  test('each of the 11 places by its key', () => {
    const { shell: model } = shell()
    for (const destination of [...destinations].reverse()) {
      expect(model.press(press(symbols(keyOf(destination)))), destination).toBe(true)
      expect(model.route).toBe(destination)
    }
  })

  test('each place by going to it, the change told once to whoever listens', () => {
    const { shell: model } = shell()
    const told: string[] = []
    model.routeChanged = (destination) => told.push(destination)
    for (const destination of destinations) model.go(destination)
    model.go('review')
    expect(told).toEqual(destinations.filter((destination) => destination !== 'sessions'))
  })

  test('each command does what its key promises', () => {
    const { shell: model } = shell()
    expect(model.press(press('⇧⌘L'))).toBe(true)
    expect(model.route).toBe('models')
    expect(model.press(press('⌥⌘M'))).toBe(true)
    expect(model.overlay).toBe('monitor')
    model.go('tasks')
    expect(model.overlay, 'moving between places leaves the monitor open').toBe('monitor')
    model.press(press('⌥⌘M'))
    expect(model.overlay).toBeUndefined()
    model.press(press('⇧⌘K'))
    expect(model.overlay).toBe('console')
    model.press(press('⌥⌘M'))
    expect(model.overlay, 'one overlay at a time').toBe('monitor')
    model.press(press('⌃⌘S'))
    expect(model.sidebarHidden).toBe(true)
    model.press(press('⌃⌘S'))
    expect(model.sidebarHidden).toBe(false)
    model.press(press('⌘,'))
    expect(model.settingsOpen).toBe(true)
    model.closeSettings()
    expect(model.settingsOpen).toBe(false)
    expect(model.press(press('⌘N')), 'a key no shell command owns is left to the page').toBe(false)
  })

  test("a key the shell does not own goes to the places' keys (design 1g), and the shell's own keys never do", () => {
    const { shell: model } = shell()
    const handed: string[] = []
    model.placeKeys = (keys) => {
      handed.push(symbols(keys))
      return keys.key === 'i'
    }
    expect(model.press(press('⌥⌘I'))).toBe(true)
    expect(model.press(press('⌘N')), 'a key no place owns is still left to the page').toBe(false)
    expect(model.press(press('⌥⌘M'))).toBe(true)
    expect(handed).toEqual(['⌥⌘I', '⌘N'])
  })

  test('the toast says one line and clears after 12 ticks of the clock; a newer one starts its own', () => {
    const { clock, shell: model } = shell()
    clock.advance(3)
    model.say('Theme: Graphite Light')
    expect(model.toast?.text).toBe('Theme: Graphite Light')
    clock.advance(12)
    expect(model.toast?.text, 'still showing at its twelfth tick').toBe('Theme: Graphite Light')
    clock.advance(1)
    expect(model.toast).toBeUndefined()
    model.say('One')
    clock.advance(10)
    model.say('Two')
    clock.advance(10)
    expect(model.toast?.text).toBe('Two')
    clock.advance(3)
    expect(model.toast).toBeUndefined()
  })

  test('the scrim closes an open menu', () => {
    const { shell: model } = shell()
    expect(model.menuOpen).toBe(false)
    model.openMenu('row:taskgroup')
    expect(model.menu).toBe('row:taskgroup')
    expect(model.menuOpen).toBe(true)
    model.closeMenus()
    expect(model.menu).toBeUndefined()
    expect(model.menuOpen).toBe(false)
  })

  test('a sheet and a confirm open by kind over the window, and close', () => {
    const { shell: model } = shell()
    model.openSheet('rename')
    model.ask('deleteChat')
    expect([model.sheet, model.confirm]).toEqual(['rename', 'deleteChat'])
    model.dismissConfirm()
    model.closeSheet()
    expect([model.sheet, model.confirm]).toEqual([undefined, undefined])
  })

  test("the system strip opens and closes, as the old sidebar's SYSTEM toggle does", () => {
    const { shell: model } = shell()
    model.toggleSystem()
    expect(model.systemOpen).toBe(true)
    model.setSystemOpen(false)
    expect(model.systemOpen).toBe(false)
  })

  test("the theme button steps the window's theme and says so in the toast, as the old cycleTheme does", () => {
    const { theme, shell: model } = shell()
    model.cycleTheme()
    expect(theme.appearance.themeChoice).toBe('light')
    expect(model.toast?.text).toBe('Theme: Graphite Light')
    model.cycleTheme()
    model.cycleTheme()
    expect(model.toast?.text).toBe('Theme: Automatic')
    expect(model.themeName).toBe('Automatic')
  })

  test('at 900 px or less the sidebar collapses to icons, and at 600 px or less a message row narrows: tokens.json\'s thresholds', () => {
    const { shell: model } = shell()
    expect(model.layout).toEqual({ collapsed: false, narrow: false })
    model.windowResized(1440)
    expect(model.layout).toEqual({ collapsed: false, narrow: false })
    model.windowResized(901)
    expect(model.layout.collapsed).toBe(false)
    model.windowResized(900)
    expect(model.layout, '900 collapses, as the old max-width: 900px does').toEqual({ collapsed: true, narrow: false })
    model.windowResized(600)
    expect(model.layout).toEqual({ collapsed: true, narrow: true })
  })

  test('every change is heard by the view', () => {
    const { shell: model } = shell()
    let heard = 0
    model.subscribe(() => (heard += 1))
    model.go('models')
    model.open('monitor')
    model.say('hello')
    model.toggleSystem()
    expect(heard).toBe(4)
  })
})
