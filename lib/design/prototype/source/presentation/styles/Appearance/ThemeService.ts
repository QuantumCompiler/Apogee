import { Observable } from '../Observation/Observable'
import type { ThemeId } from '../Tokens/Tokens'
import { defaultAppearance, effectiveTheme, rootStyle, rootVars, themeName, themeOrder } from './Appearance'
import type { Appearance, SystemScheme } from './Appearance'

/**
 * The person's appearance, held once for the whole window (design 1e): the
 * value Settings › Appearance (1i) changes and the theme button steps, which
 * the window applies -- its `data-theme` the generated theme block, its
 * inline style `rootVars()` -- so a change restyles everything at once, as
 * the old prototype's `rootVars()` does.
 */
export class ThemeService extends Observable {
  private value: Appearance
  private scheme: SystemScheme = 'dark'
  private held = false

  constructor(appearance: Appearance = defaultAppearance) {
    super()
    this.value = appearance
  }

  get appearance(): Appearance {
    return this.value
  }

  /** The viewer's light or dark setting automatic follows (design 1i): dark until the window says, or while pinned. */
  get system(): SystemScheme {
    return this.scheme
  }

  /** Whether the setting is held, so the viewer's is not followed: every start preset pins it to dark, as the old prototype draws automatic. */
  get pinned(): boolean {
    return this.held
  }

  /** The theme drawn now. */
  get theme(): ThemeId {
    return effectiveTheme(this.value, this.scheme)
  }

  /** The window's variables now: `rootVars()`. */
  get variables(): string {
    return rootVars(this.value, this.scheme)
  }

  /** The window's variables as a style object: `rootStyle()`, what the shell's view puts on the window. */
  get style(): Record<string, string> {
    return rootStyle(this.value, this.scheme)
  }

  /** The window says the viewer's setting (`prefers-color-scheme`), at first and on each change; ignored while pinned. */
  systemChanged(scheme: SystemScheme): void {
    if (this.held || scheme === this.scheme) return
    this.scheme = scheme
    this.changed()
  }

  /** Holds the setting at `scheme`, whatever the viewer's: a still frame's, so it matches the baseline. */
  pin(scheme: SystemScheme): void {
    this.held = true
    if (scheme === this.scheme) return
    this.scheme = scheme
    this.changed()
  }

  /** Changes part of the appearance: one new value. */
  update(patch: Partial<Appearance>): void {
    this.value = { ...this.value, ...patch }
    this.changed()
  }

  /** The next theme, in the theme button's order; returns its name, for the toast. */
  cycleTheme(): string {
    const next = themeOrder[(themeOrder.indexOf(this.value.themeChoice) + 1) % themeOrder.length] ?? 'dark'
    this.update({ themeChoice: next })
    return themeName(next)
  }
}
