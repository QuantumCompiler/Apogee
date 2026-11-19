import { describe, expect, test } from 'vitest'
import {
  colorGroups,
  colorsOf,
  contrastOf,
  defaultAppearance,
  effectiveTheme,
  lum,
  ratio,
  rootStyle,
  rootVars,
  themeName,
  themes,
  typeSizes,
  withOverride,
  type Appearance,
} from '../../../source/presentation/styles/Appearance/Appearance'
import { codeFonts, textSizes, themeIds, uiFonts } from '../../../source/presentation/styles/Tokens/Tokens'
import { frozenComponent, frozenRevision } from './Frozen'

/** Hex letter case aside: generated values keep tokens.json's upper case, the old prototype wrote lower. */
const folded = (text: string) => text.toLowerCase()

describe('the one-time proof: tokens.json is the frozen themes() and colorGroups()', () => {
  test(`75 colors, the three themes at ${frozenRevision.slice(0, 8)}, case aside`, () => {
    const old = frozenComponent().themes()
    const now = themes()
    let compared = 0
    for (const id of themeIds) {
      expect(now[id].name).toBe(old[id]?.name)
      expect(now[id].file).toBe(old[id]?.file)
      expect(Object.keys(now[id].colors)).toEqual(Object.keys(old[id]?.colors ?? {}))
      for (const [name, value] of Object.entries(now[id].colors)) {
        expect(folded(value), `${id} ${name}`).toBe(old[id]?.colors[name])
        compared += 1
      }
    }
    expect(compared).toBe(75)
  })

  test('the groups, 3, 3, 3, 9, 5 and 2 rows, each row its name, label, use and what its contrast is measured against', () => {
    const old = frozenComponent().colorGroups()
    const now = colorGroups()
    expect(now.map((group) => group.rows.length)).toEqual([3, 3, 3, 9, 5, 2])
    expect(now.map((group) => group.title)).toEqual(old.map((group) => group.title))
    expect(now.map((group) => group.rows.map((row) => [row.name, row.label, row.usedFor, row.contrastAgainst]))).toEqual(
      old.map((group) => group.rows),
    )
  })
})

describe('rootVars(), as the frozen one writes it', () => {
  test('3 themes × 4 text sizes × 3 interface fonts × 3 code fonts: 108 appearances, the same variables', () => {
    const old = frozenComponent()
    let compared = 0
    for (const themeChoice of themeIds) {
      for (const textSize of textSizes) {
        for (const uiFont of uiFonts) {
          for (const codeFont of codeFonts) {
            Object.assign(old.state, { themeChoice, textSize, uiFont, codeFont })
            const appearance: Appearance = { ...defaultAppearance, themeChoice, textSize, uiFont, codeFont }
            expect(folded(rootVars(appearance)), `${themeChoice} ${textSize} ${uiFont} ${codeFont}`).toBe(folded(old.rootVars()))
            compared += 1
          }
        }
      }
    }
    expect(compared).toBe(108)
  })

  test('the defaults are the old prototype\'s: dark, Plex and Plex, medium, automatic as light and dark', () => {
    const state = frozenComponent().state
    expect(defaultAppearance).toEqual({
      themeChoice: state.themeChoice,
      autoLight: state.autoLight,
      autoDark: state.autoDark,
      overrides: state.overrides,
      uiFont: state.uiFont,
      codeFont: state.codeFont,
      textSize: state.textSize,
    })
  })

  test('automatic draws its dark-uses pick, as the old effTheme() does', () => {
    const old = frozenComponent()
    for (const autoDark of themeIds) {
      Object.assign(old.state, { themeChoice: 'auto', autoDark, autoLight: 'light' })
      const appearance: Appearance = { ...defaultAppearance, themeChoice: 'auto', autoDark }
      expect(effectiveTheme(appearance)).toBe(old.effTheme())
      expect(folded(rootVars(appearance))).toBe(folded(old.rootVars()))
    }
  })

  test("a theme's edits are kept per theme, as withOverride() keeps them, and rootVars() draws them", () => {
    const old = frozenComponent()
    let appearance = defaultAppearance
    const edits: Array<[(typeof themeIds)[number], string, string | null]> = [
      ['dark', 'status.accent', '#FF00AA'],
      ['light', 'surface.window', '#FAFAFA'],
      ['dark', 'text.primary', '#E7E5E0'],
      ['dark', 'status.accent', null],
      ['contrast', 'line.border', '#777777'],
    ]
    for (const [theme, key, value] of edits) {
      const before = old.state.overrides
      old.state.overrides = old.withOverride(theme, key, value)
      expect(old.state.overrides).not.toBe(before)
      appearance = { ...appearance, overrides: withOverride(appearance, theme, key as never, value) }
      expect(appearance.overrides).toEqual(old.state.overrides)
      for (const id of themeIds) {
        Object.assign(old.state, { themeChoice: id })
        expect(folded(JSON.stringify(colorsOf(appearance, id)))).toBe(folded(JSON.stringify(old.colorsOf(id))))
        expect(folded(rootVars({ ...appearance, themeChoice: id }))).toBe(folded(old.rootVars()))
      }
    }
  })

  test('the 4 text sizes scale every --t-* size, rounded to the nearest 0.5 px', () => {
    expect(typeSizes('m').body).toBe(13)
    expect(typeSizes('s')).toEqual({ display: 22, h1: 17.5, h2: 15, h3: 13, title: 12.5, prose: 13, body: 12, dense: 11.5, small: 11, code: 11, ui: 10.5, meta: 10, label: 9.5, tag: 9 })
    expect(typeSizes('xl').body).toBe(15)
    for (const size of textSizes) {
      const variables = rootStyle({ ...defaultAppearance, textSize: size })
      for (const [step, px] of Object.entries(typeSizes(size))) expect(variables[`--t-${step}`]).toBe(`${px}px`)
    }
    for (const step of Object.keys(typeSizes('m'))) {
      if (step !== 'tag') expect(typeSizes('xl')[step as 'body']).toBeGreaterThan(typeSizes('s')[step as 'body'])
    }
  })

  test("rootStyle() is rootVars() as a style object: every variable, and the color scheme", () => {
    const style = rootStyle(defaultAppearance)
    const written = Object.entries(style)
      .map(([name, value]) => `${name === 'colorScheme' ? 'color-scheme' : name}: ${value}`)
      .join('; ')
    expect(written).toBe(rootVars(defaultAppearance))
  })
})

describe('the contrast math, as the old lum() and ratio() compute it', () => {
  test('ratio() for all 25 rows, in each theme, by each row\'s rule: text on a surface, caption on its backing, a color on what it sits on', () => {
    const old = frozenComponent()
    let compared = 0
    for (const id of themeIds) {
      const colors = colorsOf(defaultAppearance, id)
      Object.assign(old.state, { themeChoice: id })
      const oldColors = old.colorsOf(id)
      for (const group of old.colorGroups()) {
        for (const [key, , , on] of group.rows) {
          const value = oldColors[key] ?? ''
          let expected: number | null = null
          if (on === 'text') expected = old.ratio(oldColors['text.primary'] ?? '', value)
          else if (on === 'caption') expected = old.ratio(oldColors['media.caption'] ?? '', value)
          else if (on) expected = old.ratio(value, oldColors[on] ?? '')
          const row = colorGroups().flatMap((g) => g.rows).find((r) => r.name === key)
          expect(row).toBeDefined()
          if (row) expect(contrastOf(row, colors), `${id} ${key}`).toBe(expected)
          compared += 1
        }
      }
    }
    expect(compared).toBe(75)
  })

  test('lum() and ratio() agree with the old ones on every color, in either case', () => {
    const old = frozenComponent()
    for (const id of themeIds) {
      for (const value of Object.values(themes()[id].colors)) {
        expect(lum(value)).toBe(old.lum(value.toLowerCase()))
        expect(ratio(value, '#000000')).toBe(old.ratio(value.toLowerCase(), '#000000'))
      }
    }
    expect(ratio('#FFFFFF', '#000000')).toBe(21)
  })
})

describe('the theme names the shell says', () => {
  test("each theme's name, and Automatic", () => {
    expect(themeIds.map((id) => themeName(id))).toEqual(['Graphite Dark', 'Graphite Light', 'High Contrast'])
    expect(themeName('auto')).toBe('Automatic')
  })
})
