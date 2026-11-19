import { Observable } from '../Observation/Observable'
import type { ThemeId } from '../Tokens/Tokens'
import { defaultAppearance, effectiveTheme, rootVars, themeName, themeOrder } from './Appearance'
import type { Appearance } from './Appearance'

/**
 * The person's appearance, held once for the whole window (design 1e): the
 * value Settings › Appearance (1i) changes and the theme button steps, which
 * the window applies -- its `data-theme` the generated theme block, its
 * inline style `rootVars()` -- so a change restyles everything at once, as
 * the old prototype's `rootVars()` does.
 */
export class ThemeService extends Observable {
  private value: Appearance

  constructor(appearance: Appearance = defaultAppearance) {
    super()
    this.value = appearance
  }

  get appearance(): Appearance {
    return this.value
  }

  /** The theme drawn now. */
  get theme(): ThemeId {
    return effectiveTheme(this.value)
  }

  /** The window's variables now: `rootVars()`. */
  get variables(): string {
    return rootVars(this.value)
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
