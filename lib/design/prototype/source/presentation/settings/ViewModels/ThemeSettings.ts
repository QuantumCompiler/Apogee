import { colorGroups, colorsOf, contrastBadge, contrastOf, themeName, themes, typeSizes, withOverride } from '../../styles/Appearance/Appearance'
import type { ThemeChoice } from '../../styles/Appearance/Appearance'
import type { ThemeService } from '../../styles/Appearance/ThemeService'
import { Observable } from '../../styles/Observation/Observable'
import { codeFonts, textSizes, themeIds, uiFonts } from '../../styles/Tokens/Tokens'
import type { CodeFont, ColorName, TextSize, ThemeId, UiFont } from '../../styles/Tokens/Tokens'

/** A choice of a select or a segmented control: its value and its words. */
export interface Choice<Value extends string = string> {
  readonly value: Value
  readonly label: string
}

/** One theme card: the old `themeCard()`. */
export interface ThemeCard {
  readonly id: ThemeChoice
  readonly name: string
  /** `Built-in`, `Follows macOS`, or how many colors the person changed. */
  readonly note: string
  readonly noteColor: string
  readonly current: boolean
  /** Its six swatches, the old `s1`-`s6`: the window (light, for automatic), the window, primary text, the accent, the API and the subscription colors. */
  readonly swatches: readonly string[]
}

/** One element color's row: the old `colorGroups` row in the pane. */
export interface ColorRowFace {
  readonly key: ColorName
  readonly label: string
  readonly where: string
  /** The color drawn now, as the color input takes it. */
  readonly value: string
  readonly edited: boolean
  /** The theme's own color, in capitals: what Reset puts back. */
  readonly baseHex: string
  /** What the hex field shows: the draft as typed, else the color in capitals. */
  readonly hexShown: string
  readonly ratioText: string
  readonly badgeColor: string
  readonly badgeNote: string
  readonly badgeTitle: string
}

/** Everything the Appearance pane draws, as words. */
export interface AppearanceFace {
  readonly cards: readonly ThemeCard[]
  readonly auto: {
    readonly shown: boolean
    readonly line: string
    readonly light: ThemeId
    readonly dark: ThemeId
    readonly lightOptions: ReadonlyArray<Choice<ThemeId>>
    readonly darkOptions: ReadonlyArray<Choice<ThemeId>>
  }
  readonly fonts: {
    readonly ui: UiFont
    readonly code: CodeFont
    readonly uiOptions: ReadonlyArray<Choice<UiFont>>
    readonly codeOptions: ReadonlyArray<Choice<CodeFont>>
  }
  readonly sizes: ReadonlyArray<Choice<TextSize> & { readonly current: boolean }>
  readonly sizesLine: string
  readonly colors: {
    readonly title: string
    readonly file: string
    readonly changes: string
    readonly noChanges: boolean
    readonly low: string
    readonly lowColor: string
    readonly groups: ReadonlyArray<{ readonly title: string; readonly rows: readonly ColorRowFace[] }>
  }
}

/** The old pane's words for the fonts, by tokens.json's ids (`main.html`'s Interface and Code selects at `b375fac9`). */
const uiFontWords: Readonly<Record<UiFont, string>> = { plex: 'IBM Plex Sans', system: 'System font (SF Pro)', atkinson: 'Atkinson Hyperlegible' }
const codeFontWords: Readonly<Record<CodeFont, string>> = { plex: 'IBM Plex Mono', system: 'System mono (SF Mono)', jetbrains: 'JetBrains Mono' }
const sizeWords: Readonly<Record<TextSize, string>> = { s: 'Small', m: 'Medium', l: 'Large', xl: 'Extra large' }

/** What each surface is called in a row's contrast title, as the old pane names it. */
const surfaceWords: Readonly<Partial<Record<string, string>>> = { 'surface.window': 'Window', 'surface.card': 'Cards', 'surface.code': 'Code blocks', 'media.caption-fill': 'Caption backing' }

/** `n change(s)`, or `none`. */
function changes(count: number, none: string): string {
  return count === 0 ? none : `${count} ${count === 1 ? 'change' : 'changes'}`
}

/**
 * Settings › Appearance (design 1i): the theme, Automatic's two picks, both
 * fonts, the text size and every element color -- the old prototype's pane,
 * where the Mac's `ThemeSettings` holds only the theme and the text size.
 * It owns none of it: every choice is read from and written to the theme
 * service in `styles` (1e), which the window applies, so a change restyles
 * every screen at once, as the old `rootVars()` does. What it keeps is the
 * hex fields' drafts as typed, until one is a color or is left. Plain
 * TypeScript: no React.
 */
export class ThemeSettings extends Observable {
  /** The hex fields' text as typed, by `<theme>:<color>`, until it is left: the old `hexDrafts`. */
  private drafts: Readonly<Record<string, string>> = {}

  constructor(readonly service: ThemeService) {
    super()
    service.subscribe(() => this.changed())
  }

  /** The theme whose colors the pane edits: the one drawn now. */
  get edited(): ThemeId {
    return this.service.theme
  }

  pick(choice: ThemeChoice): void {
    this.service.update({ themeChoice: choice })
  }

  /** Which theme Automatic draws on a light (`light`) or dark (`dark`) system. */
  setAuto(system: 'light' | 'dark', theme: ThemeId): void {
    this.service.update(system === 'light' ? { autoLight: theme } : { autoDark: theme })
  }

  setUiFont(font: UiFont): void {
    this.service.update({ uiFont: font })
  }

  setCodeFont(font: CodeFont): void {
    this.service.update({ codeFont: font })
  }

  setTextSize(size: TextSize): void {
    this.service.update({ textSize: size })
  }

  /** The color input: the edited theme's `name` set to `value`, dropped when it is the theme's own. */
  pickColor(name: ColorName, value: string): void {
    this.service.update({ overrides: withOverride(this.service.appearance, this.edited, name, value) })
  }

  /** The hex field as typed: kept as a draft, and applied once it is six hex digits, with or without its `#`. */
  typeHex(name: ColorName, text: string): void {
    const key = `${this.edited}:${name}`
    this.drafts = { ...this.drafts, [key]: text }
    const color = /^#?([0-9a-fA-F]{6})$/.exec(text.trim())?.[1]
    if (color !== undefined) this.service.update({ overrides: withOverride(this.service.appearance, this.edited, name, `#${color}`) })
    else this.changed()
  }

  /** The hex field left: its draft dropped, so it shows the color drawn. */
  leaveHex(name: ColorName): void {
    const key = `${this.edited}:${name}`
    if (!(key in this.drafts)) return
    const { [key]: _gone, ...rest } = this.drafts
    this.drafts = rest
    this.changed()
  }

  /** A row's reset: the theme's own color back. */
  reset(name: ColorName): void {
    this.service.update({ overrides: withOverride(this.service.appearance, this.edited, name, null) })
  }

  /** Reset all changes: every color of the theme edited back, and its drafts dropped (the old `resetAll`). */
  resetAll(): void {
    this.drafts = {}
    this.service.update({ overrides: { ...this.service.appearance.overrides, [this.edited]: {} } })
  }

  face(): AppearanceFace {
    const appearance = this.service.appearance
    const edited = this.edited
    const base = themes()[edited].colors
    const colors = colorsOf(appearance, edited)
    const own = appearance.overrides[edited]
    let low = 0
    const groups = colorGroups().map((group) => ({
      title: group.title,
      rows: group.rows.map((row): ColorRowFace => {
        const value = colors[row.name]
        const ratio = contrastOf(row, colors)
        const badge = contrastBadge(ratio)
        if (badge.level === 'low' || badge.level === 'tooLow') low += 1
        const on = row.contrastAgainst
        const against = on === 'text' ? 'Primary text on this surface: ' : on === 'caption' ? 'Caption text on this backing: ' : `Against ${surfaceWords[on ?? ''] ?? on}: `
        const draft = this.drafts[`${edited}:${row.name}`]
        return {
          key: row.name,
          label: row.label,
          where: row.usedFor,
          value,
          edited: own[row.name] !== undefined,
          baseHex: base[row.name].toUpperCase(),
          hexShown: draft ?? value.toUpperCase(),
          ratioText: badge.text,
          badgeColor: badge.level === 'tooLow' ? 'var(--status-error)' : badge.level === 'low' ? 'var(--status-warning)' : 'var(--text-tertiary)',
          badgeNote: badge.note,
          badgeTitle: ratio === null ? 'Borders are decoration; no minimum applies' : `${against}${badge.text}. Text needs 4.5:1.`,
        }
      }),
    }))
    const changed = Object.keys(own).length
    const card = (id: ThemeChoice, note: string): ThemeCard => {
      const drawn = colorsOf(appearance, id === 'auto' ? appearance.autoDark : id)
      const light = id === 'auto' ? colorsOf(appearance, appearance.autoLight) : drawn
      const count = id === 'auto' ? 0 : Object.keys(appearance.overrides[id]).length
      return {
        id,
        name: themeName(id),
        note: changes(count, note),
        noteColor: count ? 'var(--status-accent)' : 'var(--text-tertiary)',
        current: appearance.themeChoice === id,
        swatches: [light['surface.window'], drawn['surface.window'], drawn['text.primary'], drawn['status.accent'], drawn['model.api'], drawn['model.subscription']],
      }
    }
    const sizes = typeSizes(appearance.textSize)
    const themeChoices = (order: readonly ThemeId[]) => order.map((id) => ({ value: id, label: themeName(id) }))
    return {
      cards: [card('auto', 'Follows macOS'), ...themeIds.map((id) => card(id, 'Built-in'))],
      auto: {
        shown: appearance.themeChoice === 'auto',
        line: `Automatic follows macOS. It’s ${this.service.system} right now.`,
        light: appearance.autoLight,
        dark: appearance.autoDark,
        lightOptions: themeChoices(['light', 'dark', 'contrast']),
        darkOptions: themeChoices(['dark', 'light', 'contrast']),
      },
      fonts: {
        ui: appearance.uiFont,
        code: appearance.codeFont,
        uiOptions: uiFonts.map((font) => ({ value: font, label: uiFontWords[font] })),
        codeOptions: codeFonts.map((font) => ({ value: font, label: codeFontWords[font] })),
      },
      sizes: textSizes.map((size) => ({ value: size, label: sizeWords[size], current: appearance.textSize === size })),
      sizesLine: `Answers ${sizes.prose} px · interface ${sizes.body} px · code ${sizes.code} px. Fonts and size are saved in config, not in themes, so a shared theme never changes your text.`,
      colors: {
        title: `ELEMENT COLORS · ${themes()[edited].name.toUpperCase()}`,
        file: themes()[edited].file,
        changes: changes(changed, 'no changes'),
        noChanges: changed === 0,
        low: low ? `${low} ${low === 1 ? 'element' : 'elements'} below 4.5:1` : 'all text meets 4.5:1',
        lowColor: low ? 'var(--status-warning)' : 'var(--text-tertiary)',
        groups,
      },
    }
  }
}
