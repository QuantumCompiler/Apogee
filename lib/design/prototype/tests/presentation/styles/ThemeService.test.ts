import { describe, expect, test } from 'vitest'
import { defaultAppearance, rootStyle, rootVars } from '../../../source/presentation/styles/Appearance/Appearance'
import { ThemeService } from '../../../source/presentation/styles/Appearance/ThemeService'
import { Observable } from '../../../source/presentation/styles/Observation/Observable'

describe('the theme service', () => {
  test('it starts at the old prototype\'s appearance, and cycles dark, light, contrast, automatic, as its theme button does', () => {
    const service = new ThemeService()
    expect(service.appearance).toEqual(defaultAppearance)
    expect([service.cycleTheme(), service.cycleTheme(), service.cycleTheme(), service.cycleTheme()]).toEqual([
      'Graphite Light',
      'High Contrast',
      'Automatic',
      'Graphite Dark',
    ])
    expect(service.appearance.themeChoice).toBe('dark')
  })

  test('a change is one new value, and every subscriber hears it', () => {
    const service = new ThemeService()
    const heard: number[] = []
    const stop = service.subscribe(() => heard.push(service.snapshot()))
    const before = service.appearance
    service.update({ textSize: 'xl', uiFont: 'atkinson' })
    expect(service.appearance).not.toBe(before)
    expect(before.textSize).toBe('m')
    expect(service.appearance.textSize).toBe('xl')
    expect(service.variables).toBe(rootVars(service.appearance))
    expect(service.variables).toContain('--t-body: 15px')
    expect(service.theme).toBe('dark')
    service.cycleTheme()
    expect(service.theme).toBe('light')
    stop()
    service.cycleTheme()
    expect(heard).toEqual([1, 2])
  })

  test("automatic follows the viewer's setting as the window reports it, unless pinned; a preset pins it to dark (design 1i)", () => {
    const service = new ThemeService({ ...defaultAppearance, themeChoice: 'auto', autoLight: 'light', autoDark: 'contrast' })
    expect(service.system, 'until the window says, dark: the old effTheme()').toBe('dark')
    expect(service.theme).toBe('contrast')
    const heard: string[] = []
    service.subscribe(() => heard.push(service.theme))
    service.systemChanged('light')
    expect(service.theme).toBe('light')
    expect(service.variables).toBe(rootVars(service.appearance, 'light'))
    expect(service.style).toEqual(rootStyle(service.appearance, 'light'))
    service.pin('dark')
    expect(service.pinned).toBe(true)
    expect(service.theme).toBe('contrast')
    service.systemChanged('light')
    expect(service.system, 'pinned: the viewer\'s setting is not followed').toBe('dark')
    expect(heard).toEqual(['light', 'contrast'])
  })

  test('it can start from another appearance, as a preview does', () => {
    expect(new ThemeService({ ...defaultAppearance, themeChoice: 'contrast' }).theme).toBe('contrast')
  })
})

describe('an observable view-model', () => {
  test('its snapshot moves on each change, and a listener that stops hears no more', () => {
    class Counter extends Observable {
      count = 0
      add() {
        this.count += 1
        this.changed()
      }
    }
    const counter = new Counter()
    const seen: number[] = []
    const stop = counter.subscribe(() => seen.push(counter.count))
    expect(counter.snapshot()).toBe(0)
    counter.add()
    counter.add()
    stop()
    counter.add()
    expect(seen).toEqual([1, 2])
    expect(counter.snapshot()).toBe(3)
  })
})
