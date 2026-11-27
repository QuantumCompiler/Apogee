import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { typePage } from '../../../source/presentation/gallery/ViewModels/TypePage'
import { iconNames } from '../../../source/presentation/styles/Icons/IconNames'
import { tokensFile } from '../../../tooling/tokens'

/** tokens.json read from disk, as the build reads it: what the page must agree with. */
const tokens = JSON.parse(readFileSync(tokensFile(resolve(dirname(fileURLToPath(import.meta.url)), '../../..')), 'utf8')) as {
  type: {
    fonts: { ui: Record<string, string>; mono: Record<string, string>; defaults: { ui: string; mono: string }; stacks: { ui: Record<string, string>; mono: Record<string, string> } }
    scalePx: Record<string, number>
    textSizes: Record<string, number>
    rounding: string
    lineHeight: Record<string, number>
  }
  spacingPx: number[]
  radiusPx: Record<string, number | string>
  controlHeightPx: Record<string, number>
  layoutPx: Record<string, number | Record<string, number>>
}

describe('the type, space and shape page, read from the tokens (design 1q)', () => {
  test('both typefaces and their alternates, by their names and stacks, the defaults marked', () => {
    const page = typePage()
    expect(page.faces.ui.map((face) => [face.id, face.name, face.stack, face.isDefault])).toEqual(
      Object.entries(tokens.type.fonts.ui).map(([id, name]) => [id, name, tokens.type.fonts.stacks.ui[id], id === tokens.type.fonts.defaults.ui]),
    )
    expect(page.faces.mono.map((face) => [face.id, face.name, face.isDefault])).toEqual(
      Object.entries(tokens.type.fonts.mono).map(([id, name]) => [id, name, id === tokens.type.fonts.defaults.mono]),
    )
  })

  test('the 14 sizes at S, M, L and XL: the scale times the text size, rounded to the nearest 0.5 px', () => {
    const page = typePage()
    expect(page.textSizes).toEqual(['S', 'M', 'L', 'XL'])
    expect(page.sizes.map((size) => size.step)).toEqual(Object.keys(tokens.type.scalePx))
    for (const size of page.sizes) {
      expect(size.px, size.step).toEqual(Object.values(tokens.type.textSizes).map((factor) => Math.round((tokens.type.scalePx[size.step] ?? 0) * factor * 2) / 2))
    }
    expect(page.sizes.find((size) => size.step === 'display')?.px).toEqual([22, 24, 26, 27.5])
    expect(page.rounding).toBe(tokens.type.rounding)
    expect(page.lineHeights).toEqual(Object.entries(tokens.type.lineHeight).map(([name, value]) => ({ name, value })))
  })

  test('the drift is fixed: spacing starts at 2 px, its 13 steps read from the tokens, and the page says so', () => {
    const page = typePage()
    expect(page.spacing).toEqual(tokens.spacingPx)
    expect(page.spacing[0]).toBe(2)
    expect(page.spacing).toHaveLength(13)
    expect(page.intro).toContain('spacing from 2 px')
    expect(page.intro).not.toContain('4 px spacing grid')
  })

  test('the radii, control heights and layout sizes, every one the tokens hold, a nested size by its path', () => {
    const page = typePage()
    expect(page.radii).toEqual(Object.entries(tokens.radiusPx).map(([name, value]) => ({ name, value: typeof value === 'number' ? `${value} px` : value })))
    expect(page.controls).toEqual(Object.entries(tokens.controlHeightPx).map(([name, px]) => ({ name, px })))
    const flat = Object.entries(tokens.layoutPx).flatMap(([name, value]) => (typeof value === 'number' ? [{ name, px: value }] : Object.entries(value).map(([part, px]) => ({ name: `${name}.${part}`, px }))))
    expect(page.layout).toEqual(flat)
    expect(page.layout.find((size) => size.name === 'settings.width')?.px).toBe(1080)
    expect(page.layout.find((size) => size.name === 'inspector')?.px).toBe(360)
  })

  test("the shadows of what floats above the window, tokens.json's fixed values", () => {
    expect(typePage().shadows.map((shadow) => shadow.name)).toEqual(['menu', 'dialog', 'toast'])
    expect(typePage().shadows[0]?.value).toBe('0 12px 32px rgba(0,0,0,0.35)')
  })

  test("the icons: whatever styles' set holds, in its order", () => {
    expect(typePage().icons).toEqual([...iconNames])
  })
})
