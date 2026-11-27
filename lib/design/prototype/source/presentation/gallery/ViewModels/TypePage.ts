import { typeSizes } from '../../styles/Appearance/Appearance'
import { iconNames } from '../../styles/Icons/IconNames'
import type { IconName } from '../../styles/Icons/IconNames'
import { codeFonts, textSizes, tokens, typeSteps, uiFonts } from '../../styles/Tokens/Tokens'
import type { TypeStep } from '../../styles/Tokens/Tokens'

/** A typeface: its id in tokens.json, its name, its stack, and whether it is the default. */
export interface Face {
  readonly id: string
  readonly name: string
  readonly stack: string
  readonly isDefault: boolean
}

export interface TypePageFace {
  /** The page's opening line, its numbers the tokens'. */
  readonly intro: string
  readonly faces: { readonly ui: readonly Face[]; readonly mono: readonly Face[] }
  /** The text sizes' columns, `S` to `XL`. */
  readonly textSizes: readonly string[]
  /** The smallest any type size lands at, at any text size. */
  readonly floor: number
  /** Each type size at each text size, in px, in the columns' order. */
  readonly sizes: ReadonlyArray<{ readonly step: TypeStep; readonly px: readonly number[] }>
  readonly rounding: string
  readonly lineHeights: ReadonlyArray<{ readonly name: string; readonly value: number }>
  readonly labelTracking: string
  /** The spacing steps, in px. */
  readonly spacing: readonly number[]
  readonly radii: ReadonlyArray<{ readonly name: string; readonly value: string }>
  readonly controls: ReadonlyArray<{ readonly name: string; readonly px: number }>
  /** The shadows of what floats above the window, by their variable's name (`menu` is `--shadow-menu`), and their CSS. */
  readonly shadows: ReadonlyArray<{ readonly name: string; readonly value: string }>
  /** The layout sizes, a nested one by its path (`settings.width`). */
  readonly layout: ReadonlyArray<{ readonly name: string; readonly px: number }>
  readonly icons: readonly IconName[]
}

/**
 * The Foundations type, space and shape page (design 1q), read from the
 * tokens: both typefaces with their alternates, the 14 sizes at each text
 * size (`styles`' `typeSizes()`, the window's own rounding), the 13 spacing
 * steps, the radii, the control heights, the shadows of what floats, the
 * layout sizes, and the icons `styles`' set holds. Nothing the tokens hold is typed by hand, so the old
 * board's drift cannot recur: its "4 px spacing grid" is the tokens' 2 px
 * first step here.
 */
export function typePage(): TypePageFace {
  const fonts = tokens.type.fonts
  const face = (kind: 'ui' | 'mono', id: string): Face => ({
    id,
    name: (fonts[kind] as Record<string, string>)[id] ?? id,
    stack: (fonts.stacks[kind] as Record<string, string>)[id] ?? '',
    isDefault: fonts.defaults[kind] === id,
  })
  const perSize = textSizes.map((size) => typeSizes(size))
  const spacing = [...tokens.spacingPx]
  const body = typeSizes('m').body
  return {
    intro: `Two typefaces, ${body} px interface text by default, and spacing from ${spacing[0] ?? 0} px. Every size here moves with Settings › Appearance › Text size; spacing and shape stay fixed.`,
    faces: { ui: uiFonts.map((id) => face('ui', id)), mono: codeFonts.map((id) => face('mono', id)) },
    textSizes: textSizes.map((size) => size.toUpperCase()),
    // The smallest any size lands at, at any text size: the old board's "Nothing goes below 9 px" (design 1s), read from the tokens.
    floor: Math.min(...perSize.flatMap((sizes) => typeSteps.map((step) => sizes[step]))),
    sizes: typeSteps.map((step) => ({ step, px: perSize.map((sizes) => sizes[step]) })),
    rounding: tokens.type.rounding,
    lineHeights: Object.entries(tokens.type.lineHeight).map(([name, value]) => ({ name, value })),
    labelTracking: tokens.type.labelTracking,
    spacing,
    radii: Object.entries(tokens.radiusPx).map(([name, value]) => ({ name, value: typeof value === 'number' ? `${value} px` : value })),
    controls: Object.entries(tokens.controlHeightPx).map(([name, px]) => ({ name, px })),
    shadows: [
      { name: 'menu', value: tokens.fixed.menuShadow },
      { name: 'dialog', value: tokens.fixed.dialogShadow },
      { name: 'toast', value: tokens.fixed.toastShadow },
    ],
    layout: Object.entries(tokens.layoutPx).flatMap(([name, value]) =>
      typeof value === 'number' ? [{ name, px: value }] : Object.entries(value).map(([part, px]) => ({ name: `${name}.${part}`, px: px as number })),
    ),
    icons: [...iconNames],
  }
}
