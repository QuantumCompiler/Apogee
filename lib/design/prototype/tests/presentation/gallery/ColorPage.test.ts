import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { colorPage } from '../../../source/presentation/gallery/ViewModels/ColorPage'
import { defaultAppearance, ratio, withOverride } from '../../../source/presentation/styles/Appearance/Appearance'
import { tokensFile } from '../../../tooling/tokens'

/** tokens.json read from disk, as the build reads it: what the page must agree with. */
const tokens = JSON.parse(readFileSync(tokensFile(resolve(dirname(fileURLToPath(import.meta.url)), '../../..')), 'utf8')) as {
  colors: {
    groups: Array<{ group: string; tokens: Array<{ name: string; label: string; usedFor: string; contrastAgainst: string | null }> }>
    themes: Record<string, { name: string; colors: Record<string, string> }>
  }
}

/**
 * The pair a token's contrast is measured on, by tokens.json's own words:
 * `text` is primary text on the surface, `caption` the caption on its
 * backing (main.html's colorGroups() reading at b375fac9), a token's name
 * that token, and nothing a border.
 */
function pair(row: { name: string; contrastAgainst: string | null }, colors: Record<string, string>): [string, string] | undefined {
  const value = colors[row.name] ?? ''
  if (row.contrastAgainst === null) return undefined
  if (row.contrastAgainst === 'text') return [colors['text.primary'] ?? '', value]
  if (row.contrastAgainst === 'caption') return [colors['media.caption'] ?? '', value]
  return [value, colors[row.contrastAgainst] ?? '']
}

describe('the color page: the 25 tokens × 3 themes, every ratio computed (design 1q)', () => {
  test('the six groups of tokens.json, 3, 3, 3, 9, 5 and 2, each row in its order, under the three themes by their names', () => {
    const page = colorPage()
    expect(page.count).toBe(25)
    expect(page.themes.map((theme) => theme.name)).toEqual(['Graphite Dark', 'Graphite Light', 'High Contrast'])
    expect(page.groups.map((group) => [group.title, group.rows.length])).toEqual([
      ['ACCENT & STATUS', 3],
      ['MODEL TYPES', 3],
      ['TEXT', 3],
      ['SURFACES', 9],
      ['CODE', 5],
      ['MEDIA', 2],
    ])
    expect(page.groups.flatMap((group) => group.rows.map((row) => row.name))).toEqual(tokens.colors.groups.flatMap((group) => group.tokens.map((row) => row.name)))
    for (const group of page.groups) for (const row of group.rows) expect(row.cells.map((cell) => cell.theme)).toEqual(['dark', 'light', 'contrast'])
  })

  test("all 75 cells agree with ratio() over tokens.json's values: 69 carry their ratio, and line.border and line.strong a dash in each theme", () => {
    const page = colorPage()
    const rows = tokens.colors.groups.flatMap((group) => group.tokens)
    let ratios = 0
    let dashes = 0
    for (const row of rows) {
      const shown = page.groups.flatMap((group) => group.rows).find((each) => each.name === row.name)
      for (const [theme, { colors }] of Object.entries(tokens.colors.themes)) {
        const cell = shown?.cells.find((each) => each.theme === theme)
        expect(cell?.hex, `${row.name} in ${theme}`).toBe(colors[row.name])
        const measured = pair(row, colors)
        if (measured === undefined) {
          expect([cell?.ratio, cell?.text], `${row.name} in ${theme}`).toEqual([null, '—'])
          dashes += 1
        } else {
          const expected = ratio(...measured)
          expect(cell?.ratio, `${row.name} in ${theme}`).toBe(expected)
          expect(cell?.text, `${row.name} in ${theme}`).toBe(`${expected.toFixed(1)}:1`)
          ratios += 1
        }
      }
    }
    expect([ratios, dashes]).toEqual([69, 6])
  })

  test('each row says what it is measured against: a token by name, `text` as primary text on it, `caption` as the caption on its backing, a border as decoration', () => {
    const against = Object.fromEntries(colorPage().groups.flatMap((group) => group.rows.map((row) => [row.name, row.against])))
    expect(against['status.accent']).toBe('surface.window')
    expect(against['text.secondary']).toBe('surface.card')
    expect(against['surface.window']).toBe('text.primary on it')
    expect(against['media.caption']).toBe('media.caption-fill')
    expect(against['media.caption-fill']).toBe('media.caption on it')
    expect(against['line.border']).toBe('decoration, no minimum')
    expect(against['code.keyword']).toBe('surface.code')
  })

  test('a planted override changes the shown ratio, with no edit to the page', () => {
    const before = colorPage()
    const planted = colorPage({ ...defaultAppearance, overrides: withOverride(defaultAppearance, 'dark', 'surface.window', '#3B3E42') })
    const cell = (page: typeof before, name: string) => page.groups.flatMap((group) => group.rows).find((row) => row.name === name)?.cells[0]
    expect(cell(before, 'status.accent')?.text).toBe('10.6:1')
    expect(cell(planted, 'status.accent')?.hex).toBe('#7FD4C1')
    expect(cell(planted, 'status.accent')?.text).toBe(`${ratio('#7FD4C1', '#3b3e42').toFixed(1)}:1`)
    expect(cell(planted, 'surface.window')?.hex).toBe('#3b3e42')
    expect(cell(planted, 'surface.window')?.text).not.toBe(cell(before, 'surface.window')?.text)
  })

  test("a ratio under 4.5:1 is badged low, and under 3:1 too low, as Settings › Appearance's badge says it", () => {
    const planted = colorPage({ ...defaultAppearance, overrides: withOverride(defaultAppearance, 'dark', 'text.tertiary', '#5a5750') })
    const cell = planted.groups.flatMap((group) => group.rows).find((row) => row.name === 'text.tertiary')?.cells[0]
    expect(cell?.note).toBe(ratio('#5a5750', '#18191B') < 3 ? 'too low' : 'low')
  })

  test("the drift is fixed: status.warning's use reads tokens.json's words", () => {
    const warning = colorPage().groups.flatMap((group) => group.rows).find((row) => row.name === 'status.warning')
    expect(warning?.usedFor).toBe('Context at 80% and above, waiting for you')
    expect(warning?.label).toBe('Warning')
  })
})
