import { tokens } from '@tokens'
import type { CodeFont, ColorName, TextSize, ThemeId, TypeStep, UiFont } from '@tokens'

/**
 * The design's tokens, typed (design 1e): tokens.json as the build generates
 * it (`@tokens`, tooling/tokens.ts), and the names each kind of token goes by,
 * in tokens.json's order. Every other module takes its tokens from here:
 * only `styles` reads `@tokens`.
 */
export { tokens }
export type { CodeFont, ColorName, TextSize, ThemeId, TypeStep, UiFont }

/** `dark`, `light`, `contrast`. */
export const themeIds = Object.keys(tokens.colors.themes) as ThemeId[]

/** The 25 colors, in the themes' order. */
export const colorNames = Object.keys(tokens.colors.themes.dark.colors) as ColorName[]

/** The 14 type sizes, largest first. */
export const typeSteps = Object.keys(tokens.type.scalePx) as TypeStep[]

/** `s`, `m`, `l`, `xl`. */
export const textSizes = Object.keys(tokens.type.textSizes) as TextSize[]

/** `plex`, `system`, `atkinson`. */
export const uiFonts = Object.keys(tokens.type.fonts.stacks.ui) as UiFont[]

/** `plex`, `system`, `jetbrains`. */
export const codeFonts = Object.keys(tokens.type.fonts.stacks.mono) as CodeFont[]

/** The layout sizes, in px. */
export const layout = tokens.layoutPx
