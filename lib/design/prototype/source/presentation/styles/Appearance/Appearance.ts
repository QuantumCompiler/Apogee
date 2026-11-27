import { colorNames, tokens, typeSteps } from '../Tokens/Tokens'
import type { CodeFont, ColorName, TextSize, ThemeId, TypeStep, UiFont } from '../Tokens/Tokens'

/**
 * How the person has the window look (design 1e): the old prototype's
 * appearance state as one plain value -- the theme (or automatic, with its
 * light-uses and dark-uses picks), each theme's edited colors, both fonts and
 * the text size -- and the old prototype's theme machinery as pure functions
 * over it (`themes()`, `colorGroups()`, `lum()`, `ratio()`, `withOverride()`,
 * `rootVars()`, the old prototype's `main.html` at `b375fac9`), every value
 * from tokens.json. Settings › Appearance (1i) changes it through the theme
 * service; the gallery's color and type pages (1q) read it.
 */
export interface Appearance {
  readonly themeChoice: ThemeChoice
  /** The theme automatic draws on a light system; `light` unless picked. */
  readonly autoLight: ThemeId
  /** The theme automatic draws on a dark system; `dark` unless picked. */
  readonly autoDark: ThemeId
  /** Each theme's edited colors, lower-case hex as `withOverride` stores them. */
  readonly overrides: Readonly<Record<ThemeId, Readonly<Partial<Record<ColorName, string>>>>>
  readonly uiFont: UiFont
  readonly codeFont: CodeFont
  readonly textSize: TextSize
}

/** A theme, or automatic. */
export type ThemeChoice = ThemeId | 'auto'

/**
 * The viewer's light or dark setting, which automatic follows (design 1i):
 * what `prefers-color-scheme` says. Unsaid, it is dark, as the old
 * prototype's `effTheme()` always drew automatic.
 */
export type SystemScheme = 'light' | 'dark'

/** The old prototype's starting state: Graphite Dark, IBM Plex for both fonts, medium text, nothing edited. */
export const defaultAppearance: Appearance = {
  themeChoice: 'dark',
  autoLight: 'light',
  autoDark: 'dark',
  overrides: { dark: {}, light: {}, contrast: {} },
  uiFont: tokens.type.fonts.defaults.ui as UiFont,
  codeFont: tokens.type.fonts.defaults.mono as CodeFont,
  textSize: 'm',
}

/** The order the sidebar's theme button steps through, as the old `cycleTheme` does. */
export const themeOrder: readonly ThemeChoice[] = ['dark', 'light', 'contrast', 'auto']

/** A theme: its name, its file name and its 25 colors, in tokens.json's letter case. */
export interface Theme {
  readonly name: string
  readonly file: string
  readonly colors: Readonly<Record<ColorName, string>>
}

/** The three themes, as tokens.json holds them: the old `themes()`. */
export function themes(): Readonly<Record<ThemeId, Theme>> {
  return tokens.colors.themes
}

/** One color's row: the old `colorGroups()` row, `[name, label, usedFor, contrastAgainst]`. */
export interface ColorRow {
  readonly name: ColorName
  readonly label: string
  readonly usedFor: string
  /** What its contrast is measured against: a color, `text` (primary text on it), `caption`, or nothing (a border). */
  readonly contrastAgainst: string | null
}

/** The colors in their groups, as tokens.json lists them: the old `colorGroups()`, its titles in capitals. */
export function colorGroups(): ReadonlyArray<{ readonly title: string; readonly rows: readonly ColorRow[] }> {
  return tokens.colors.groups.map((group) => ({
    title: group.group.toUpperCase(),
    rows: group.tokens.map((row) => ({
      name: row.name as ColorName,
      label: row.label,
      usedFor: row.usedFor,
      contrastAgainst: row.contrastAgainst,
    })),
  }))
}

/** A color's relative luminance (WCAG 2): the old `lum()`. */
export function lum(hex: string): number {
  const n = parseInt(hex.slice(1), 16)
  const channels = [(n >> 16) & 255, (n >> 8) & 255, n & 255].map((value) => {
    const c = value / 255
    return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
  })
  return 0.2126 * (channels[0] ?? 0) + 0.7152 * (channels[1] ?? 0) + 0.0722 * (channels[2] ?? 0)
}

/** The contrast of two colors, 1 to 21: the old `ratio()`. */
export function ratio(a: string, b: string): number {
  const x = lum(a)
  const y = lum(b)
  return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05)
}

/**
 * A row's contrast in `colors`, by its rule, as the old Appearance pane
 * measures it: primary text on a surface (`text`), a caption on its backing
 * (`caption`), a color on what it sits on; `null` for a border.
 */
export function contrastOf(row: ColorRow, colors: Readonly<Record<ColorName, string>>): number | null {
  const value = colors[row.name]
  const on = row.contrastAgainst
  if (on === 'text') return ratio(colors['text.primary'], value)
  if (on === 'caption') return ratio(colors['media.caption'], value)
  if (on) return ratio(value, colors[on as ColorName])
  return null
}

/**
 * The theme drawn: the choice, or for automatic the pick for the viewer's
 * setting -- light uses, or dark uses (design 1i). Unsaid, the setting is
 * dark, so automatic draws its dark-uses pick, as the old `effTheme()` does.
 */
export function effectiveTheme(appearance: Appearance, system: SystemScheme = 'dark'): ThemeId {
  if (appearance.themeChoice !== 'auto') return appearance.themeChoice
  return system === 'light' ? appearance.autoLight : appearance.autoDark
}

/** A theme's colors with the person's edits: the old `colorsOf()`. */
export function colorsOf(appearance: Appearance, id: ThemeId): Record<ColorName, string> {
  const base = themes()[id].colors
  const edited = appearance.overrides[id]
  const out = {} as Record<ColorName, string>
  for (const name of colorNames) out[name] = edited[name] ?? base[name]
  return out
}

/**
 * Every theme's edits with `key` in `theme` set to `value` -- or dropped, for
 * `null` or the theme's own color: the old `withOverride()`, a new value
 * each time, its hex stored in lower case.
 */
export function withOverride(appearance: Appearance, theme: ThemeId, key: ColorName, value: string | null): Appearance['overrides'] {
  const base = themes()[theme].colors[key].toLowerCase()
  const mine: Partial<Record<ColorName, string>> = { ...appearance.overrides[theme] }
  if (value === null || value.toLowerCase() === base) delete mine[key]
  else mine[key] = value.toLowerCase()
  return { ...appearance.overrides, [theme]: mine }
}

/** How much the text size scales every type size: tokens.json's `textSizes`. */
export function textSizeFactor(size: TextSize): number {
  return tokens.type.textSizes[size]
}

/** Each type size at `size`, in px: the scale times the factor, rounded to the nearest 0.5 px. */
export function typeSizes(size: TextSize): Record<TypeStep, number> {
  const factor = textSizeFactor(size)
  const out = {} as Record<TypeStep, number>
  for (const step of typeSteps) out[step] = Math.round(tokens.type.scalePx[step] * factor * 2) / 2
  return out
}

/** The theme's name, or Automatic: what the theme button and its toast say. */
export function themeName(choice: ThemeChoice): string {
  return choice === 'auto' ? 'Automatic' : themes()[choice].name
}

/**
 * A row's contrast as the old Appearance pane badges it (design 1i, shared
 * with 1q's color page): under 3:1 `too low`, under 4.5:1 `low` -- text needs
 * 4.5:1 -- and a border, which no minimum applies to, none; its ratio to one
 * decimal, as `ratio.toFixed(1) + ':1'`.
 */
export interface ContrastBadge {
  readonly level: 'none' | 'ok' | 'low' | 'tooLow'
  readonly note: '' | 'low' | 'too low'
  readonly text: string
}

export function contrastBadge(ratio: number | null): ContrastBadge {
  if (ratio === null) return { level: 'none', note: '', text: '—' }
  const text = `${ratio.toFixed(1)}:1`
  if (ratio < 3) return { level: 'tooLow', note: 'too low', text }
  if (ratio < 4.5) return { level: 'low', note: 'low', text }
  return { level: 'ok', note: '', text }
}

/** What `rootVars` writes, in its order: the 25 colors, the 14 type sizes, both fonts, the color scheme. */
function rootDeclarations(appearance: Appearance, system: SystemScheme): Array<[string, string]> {
  const id = effectiveTheme(appearance, system)
  const colors = colorsOf(appearance, id)
  const out: Array<[string, string]> = colorNames.map((name) => [`--${name.replace('.', '-')}`, colors[name]])
  for (const [step, px] of Object.entries(typeSizes(appearance.textSize))) out.push([`--t-${step}`, `${px}px`])
  out.push(['--font-ui', tokens.type.fonts.stacks.ui[appearance.uiFont]])
  out.push(['--font-mono', tokens.type.fonts.stacks.mono[appearance.codeFont]])
  out.push(['color-scheme', id === 'light' ? 'light' : 'dark'])
  return out
}

/**
 * The window's variables for `appearance`, as one inline style: the old
 * `rootVars()`, word for word but for the hex's letter case. Every color, type
 * size and font the views use is one of these.
 */
export function rootVars(appearance: Appearance, system: SystemScheme = 'dark'): string {
  return rootDeclarations(appearance, system)
    .map(([name, value]) => `${name}: ${value}`)
    .join('; ')
}

/** `rootVars()` as a React style object: the variables by name, and `colorScheme`. */
export function rootStyle(appearance: Appearance, system: SystemScheme = 'dark'): Record<string, string> {
  const out: Record<string, string> = {}
  for (const [name, value] of rootDeclarations(appearance, system)) out[name === 'color-scheme' ? 'colorScheme' : name] = value
  return out
}
