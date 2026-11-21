import { colorGroups, colorsOf, contrastBadge, contrastOf, defaultAppearance, themes } from '../../styles/Appearance/Appearance'
import type { Appearance, ColorRow } from '../../styles/Appearance/Appearance'
import { colorNames, themeIds } from '../../styles/Tokens/Tokens'
import type { ColorName, ThemeId } from '../../styles/Tokens/Tokens'

/** One token in one theme: its value, its contrast by its rule, and the badge Settings › Appearance gives it. */
export interface ColorCell {
  readonly theme: ThemeId
  readonly hex: string
  /** The ratio `ratio()` gives, 1 to 21; `null` for a border, which no minimum applies to. */
  readonly ratio: number | null
  /** `10.6:1`, or a dash for a border. */
  readonly text: string
  readonly note: '' | 'low' | 'too low'
}

export interface ColorPageRow {
  readonly name: ColorName
  readonly label: string
  readonly usedFor: string
  /** What its ratio is measured against, in words. */
  readonly against: string
  readonly cells: readonly ColorCell[]
}

export interface ColorPageGroup {
  readonly title: string
  readonly rows: readonly ColorPageRow[]
}

export interface ColorPageFace {
  readonly count: number
  readonly themes: ReadonlyArray<{ readonly id: ThemeId; readonly name: string; readonly file: string }>
  readonly groups: readonly ColorPageGroup[]
}

/**
 * What a row's ratio is measured against, in words. tokens.json's
 * `contrastAgainst` names a token, or one of two words the old prototype
 * read as pairs (`main.html` ~6146-6149 at `b375fac9`, kept by `styles`'
 * `contrastOf`): `text`, primary text (`text.primary`) on the surface, and
 * `caption`, the caption (`media.caption`) on its backing. A border names
 * nothing: it is decoration, with no minimum.
 */
function againstWords(row: ColorRow): string {
  if (row.contrastAgainst === 'text') return 'text.primary on it'
  if (row.contrastAgainst === 'caption') return 'media.caption on it'
  if (row.contrastAgainst) return row.contrastAgainst
  return 'decoration, no minimum'
}

/**
 * The Foundations color page (design 1q), generated from the tokens: the 25
 * colors in tokens.json's six groups, each with its label, what it is used
 * for and what it is measured against, and its value, ratio and badge in each
 * of the three themes -- the ratio `styles`' `ratio()` computes through
 * `contrastOf()` and badged by `contrastBadge()`, the same functions
 * Settings › Appearance's badges use, so nothing is typed by hand. The
 * appearance's edits count, as the window's would: an override changes the
 * ratio shown with no edit to the page.
 */
export function colorPage(appearance: Appearance = defaultAppearance): ColorPageFace {
  const named = themes()
  const palettes = Object.fromEntries(themeIds.map((theme) => [theme, colorsOf(appearance, theme)])) as Record<ThemeId, Record<ColorName, string>>
  return {
    count: colorNames.length,
    themes: themeIds.map((id) => ({ id, name: named[id].name, file: named[id].file })),
    groups: colorGroups().map((group) => ({
      title: group.title,
      rows: group.rows.map((row) => ({
        name: row.name,
        label: row.label,
        usedFor: row.usedFor,
        against: againstWords(row),
        cells: themeIds.map((theme): ColorCell => {
          const colors = palettes[theme]
          const ratio = contrastOf(row, colors)
          const badge = contrastBadge(ratio)
          return { theme, hex: colors[row.name], ratio, text: badge.text, note: badge.note }
        }),
      })),
    })),
  }
}
