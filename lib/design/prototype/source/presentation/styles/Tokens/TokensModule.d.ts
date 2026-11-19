// The types of the two modules the build generates from tokens.json (design
// 1e, tooling/tokens.ts). The module is generated at build time and never
// committed, yet `tsc --noEmit` runs before any build, so its types are
// declared here -- taken from tokens.json itself, through tsconfig.json's
// `paths`, so they can never drift from the file. Only `styles` imports
// `@tokens`; every other module takes the tokens from it.

declare module '@tokens' {
  /** tokens.json, as the type checker reads it. */
  type File = typeof import('@tokens/tokens.json')

  /** tokens.json, value for value. */
  export const tokens: File

  /** `dark`, `light`, `contrast`. */
  export type ThemeId = keyof File['colors']['themes']
  /** The 25 colors: `surface.window` … `media.caption-fill`. */
  export type ColorName = keyof File['colors']['themes']['dark']['colors']
  /** The 14 type sizes: `display` … `tag`. */
  export type TypeStep = keyof File['type']['scalePx']
  /** The 4 text sizes: `s`, `m`, `l`, `xl`. */
  export type TextSize = keyof File['type']['textSizes']
  /** The 3 interface fonts: `plex`, `system`, `atkinson`. */
  export type UiFont = keyof File['type']['fonts']['stacks']['ui']
  /** The 3 code fonts: `plex`, `system`, `jetbrains`. */
  export type CodeFont = keyof File['type']['fonts']['stacks']['mono']
}

declare module '@tokens/themes.css' {}
