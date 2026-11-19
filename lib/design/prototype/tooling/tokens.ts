import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import type { Plugin } from 'vite'

/**
 * `@tokens`, the named root of the design's tokens (design 1e): the one file
 * outside the prototype the build reads besides the recordings,
 * `lib/design/spec/json/tokens.json`, the only place a color, size, font,
 * radius, motion or fixed value is written. Its three forms:
 *
 * - `@tokens`: the typed tokens module, generated from the file at build
 *   time (`tokensModule`), typed by the committed declaration
 *   `source/presentation/styles/Tokens/TokensModule.d.ts`;
 * - `@tokens/themes.css`: the CSS custom properties, one block per theme and
 *   one shared block (`themesCss`);
 * - `@tokens/tokens.json`: the file itself, which only that declaration names,
 *   for its types (tsconfig.json's `paths`).
 *
 * Nothing generated is written to disk or committed. 1t moves the file
 * beside the built one. `tokensRoot` and `tsconfig.json`'s `paths` entry for
 * `@tokens/tokens.json` are the two lines that say where it is, and a move
 * changes both: JSON cannot compute a path, and with `skipLibCheck` a stale
 * `paths` entry turns the token types into `any` instead of reporting a
 * missing file. Everything else reads the path through `tokensFile` (the
 * Playwright tests too); `Tokens.test.ts` and `stale.test.ts` pin it on
 * purpose.
 */
export const tokensAlias = '@tokens'

/** The generated stylesheet's specifier. */
export const themesCssSpecifier = `${tokensAlias}/themes.css`

/** The file's specifier, for the declaration's types. */
export const tokensJsonSpecifier = `${tokensAlias}/tokens.json`

/** Where the tokens live, from the prototype's root (`lib/design/prototype`). */
export function tokensRoot(root: string): string {
  return resolve(root, '../spec/json')
}

/** `tokens.json`, from the prototype's root. */
export function tokensFile(root: string): string {
  return resolve(tokensRoot(root), 'tokens.json')
}

/** The parts of tokens.json the build reads to write CSS; the rest rides along in the typed module. */
export interface TokensJson {
  readonly colors: { readonly themes: Readonly<Record<string, { readonly colors: Readonly<Record<string, string>> }>> }
  readonly type: {
    readonly scalePx: Readonly<Record<string, number>>
    readonly fonts: {
      readonly defaults: { readonly ui: string; readonly mono: string }
      readonly stacks: { readonly ui: Readonly<Record<string, string>>; readonly mono: Readonly<Record<string, string>> }
    }
  }
  readonly radiusPx: Readonly<Record<string, number | string>>
  readonly controlHeightPx: Readonly<Record<string, number>>
  readonly layoutPx: Readonly<Record<string, number | Readonly<Record<string, number>>>>
  /** Each a duration and timing function, then, for the caret and the pulse, words about the rest. */
  readonly motion: { readonly chevron: string; readonly caret: string; readonly pulse: string }
  readonly fixed: {
    readonly modalScrim: string
    readonly menuShadow: string
    readonly dialogShadow: string
    readonly toastShadow: string
    readonly windowControlColors: Readonly<Record<string, string>>
    readonly captionFillOpacity: number
  }
}

/** tokens.json, parsed. */
export function readTokens(root: string): TokensJson {
  return JSON.parse(readFileSync(tokensFile(root), 'utf8')) as TokensJson
}

/** `surface.window` -> `--surface-window`, as the old `rootVars()` names a color (its first `.` only). */
export function colorVariable(name: string): string {
  return `--${name.replace('.', '-')}`
}

/** `sidebarCollapsed` -> `sidebar-collapsed`. */
function kebab(name: string): string {
  return name.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`)
}

/** A length in px, or a token that is already CSS (`50%`). */
function length(value: number | string): string {
  return typeof value === 'number' ? `${value}px` : value
}

/** A motion entry's leading duration and timing function: `1.4s ease-in-out` of `1.4s ease-in-out, opacity 1 to 0.3`. */
const motionPattern = /^(\d*\.?\d+m?s (?:ease-in-out|ease-in|ease-out|ease|linear|steps\(\d+(?:, ?[a-z-]+)?\)|cubic-bezier\([\d., -]+\)))(?=[\s,]|$)/

/**
 * The CSS a motion entry gives (design 1e): its leading duration and timing
 * function, the part a `transition` or `animation` takes. tokens.json writes
 * the caret's and the pulse's rest as words (`blink`, `opacity 1 to 0.3`),
 * which the helmet's keyframes draw; an entry with no CSS at its head is
 * refused, so words never reach a stylesheet.
 */
export function motionValue(name: string, value: string): string {
  const css = motionPattern.exec(value)?.[1]
  if (css === undefined) {
    throw new Error(`tokens.json motion.${name} is "${value}": a motion token starts with a CSS duration and timing function (1.4s ease-in-out)`)
  }
  return css
}

/** The shared block's declarations, in order: what every theme has alike. */
function sharedDeclarations(tokens: TokensJson): Array<[string, string]> {
  const out: Array<[string, string]> = []
  for (const [name, value] of Object.entries(tokens.layoutPx)) {
    if (typeof value === 'number') out.push([`--layout-${kebab(name)}`, length(value)])
    else for (const [part, size] of Object.entries(value)) out.push([`--layout-${kebab(name)}-${kebab(part)}`, length(size)])
  }
  for (const [name, value] of Object.entries(tokens.radiusPx)) out.push([`--radius-${kebab(name)}`, length(value)])
  for (const [name, value] of Object.entries(tokens.controlHeightPx)) out.push([`--control-${kebab(name)}`, length(value)])
  for (const [name, value] of Object.entries(tokens.type.scalePx)) out.push([`--t-${name}`, length(value)])
  const ui = tokens.type.fonts.stacks.ui[tokens.type.fonts.defaults.ui]
  const mono = tokens.type.fonts.stacks.mono[tokens.type.fonts.defaults.mono]
  if (ui !== undefined) out.push(['--font-ui', ui])
  if (mono !== undefined) out.push(['--font-mono', mono])
  out.push(['--scrim-modal', tokens.fixed.modalScrim])
  out.push(['--shadow-menu', tokens.fixed.menuShadow])
  out.push(['--shadow-dialog', tokens.fixed.dialogShadow])
  out.push(['--shadow-toast', tokens.fixed.toastShadow])
  for (const [name, value] of Object.entries(tokens.fixed.windowControlColors)) out.push([`--window-${kebab(name)}`, value])
  out.push(['--caption-fill-opacity', String(tokens.fixed.captionFillOpacity)])
  for (const name of ['chevron', 'caret', 'pulse'] as const) out.push([`--motion-${name}`, motionValue(name, tokens.motion[name])])
  return out
}

/** A theme's block's declarations: its 25 colors, in tokens.json's order and letter case, then its color scheme. */
function themeDeclarations(id: string, colors: Readonly<Record<string, string>>): Array<[string, string]> {
  return [...Object.entries(colors).map(([name, value]): [string, string] => [colorVariable(name), value]), ['color-scheme', id === 'light' ? 'light' : 'dark']]
}

/** Every block the generated CSS holds, by selector: `:root`, then `[data-theme="<id>"]` per theme. */
function blocks(tokens: TokensJson): Array<[string, Array<[string, string]>]> {
  return [
    [':root', sharedDeclarations(tokens)],
    ...Object.entries(tokens.colors.themes).map(([id, theme]): [string, Array<[string, string]>] => [
      `[data-theme="${id}"]`,
      themeDeclarations(id, theme.colors),
    ]),
  ]
}

/**
 * The CSS custom properties tokens.json defines (design 1e): one shared block
 * on `:root` -- the layout sizes, radii, control heights, the type scale at
 * the medium text size, the default fonts, the fixed values and the motion
 * (`--motion-chevron`, `--motion-caret`, `--motion-pulse`) -- and one
 * block per theme, `[data-theme="<id>"]`, with its 25 colors and color scheme.
 * The theme service puts `data-theme` on the window, and `rootVars()` the
 * person's text size, fonts and edits beside it.
 */
export function themesCss(tokens: TokensJson): string {
  const head = '/* Generated from lib/design/spec/json/tokens.json by tooling/tokens.ts at build time: never edited, never committed. */\n'
  return (
    head +
    blocks(tokens)
      .map(([selector, declarations]) => `${selector} {\n${declarations.map(([name, value]) => `  ${name}: ${value};`).join('\n')}\n}\n`)
      .join('')
  )
}

/** The typed tokens module's code: tokens.json as one value, typed by `TokensModule.d.ts`. */
export function tokensModule(tokens: TokensJson): string {
  return `// Generated from lib/design/spec/json/tokens.json by tooling/tokens.ts at build time: never edited, never committed.\nexport const tokens = ${JSON.stringify(tokens)}\n`
}

/** The declarations of each block in `css`, by selector: what a browser would read. */
function parsedBlocks(css: string): Map<string, Map<string, string>> {
  const found = new Map<string, Map<string, string>>()
  const text = css.replace(/\/\*[\s\S]*?\*\//g, '')
  for (const match of text.matchAll(/([^{}]+)\{([^{}]*)\}/g)) {
    const selector = (match[1] ?? '').trim().replace(/\[data-theme=([a-z]+)\]/, '[data-theme="$1"]')
    const declarations = found.get(selector) ?? new Map<string, string>()
    for (const declaration of (match[2] ?? '').split(';')) {
      const colon = declaration.indexOf(':')
      if (colon < 0) continue
      declarations.set(declaration.slice(0, colon).trim(), declaration.slice(colon + 1).trim())
    }
    found.set(selector, declarations)
  }
  return found
}

/**
 * The token check (design 1e): whether `css` -- the generated stylesheet, or
 * the one the built file carries -- says what tokens.json says, value for
 * value. Each difference is a sentence; none when they agree. Only the blocks
 * the build generates are read; any other rule in `css` is left alone.
 */
export function tokenCheck(css: string, tokens: TokensJson): string[] {
  const found: string[] = []
  const parsed = parsedBlocks(css)
  for (const [selector, declarations] of blocks(tokens)) {
    const actual = parsed.get(selector)
    if (!actual) {
      found.push(`${selector} is missing: tokens.json has ${declarations.length} values for it`)
      continue
    }
    const expected = new Map(declarations)
    for (const [name, value] of expected) {
      const got = actual.get(name)
      if (got === undefined) found.push(`${selector} has no ${name}: tokens.json says ${value}`)
      else if (got !== value) found.push(`${selector} ${name} is ${got}, and tokens.json says ${value}`)
    }
    for (const name of actual.keys()) {
      if (!expected.has(name)) found.push(`${selector} ${name} is not in tokens.json`)
    }
  }
  return found
}

/** The ids Vite knows the two generated modules by. */
const moduleId = '\0apogee-tokens.js'
const cssId = '\0apogee-tokens-themes.css'

/**
 * The build step (design 1e): serves `@tokens` and `@tokens/themes.css`,
 * generated from tokens.json each time they are read, so a token edit reaches
 * the next build and the dev server reloads. Both the build and the tests use it.
 */
export function tokens({ root }: { root: string }): Plugin {
  return {
    name: 'apogee-tokens',
    enforce: 'pre',
    resolveId(specifier) {
      if (specifier === tokensAlias) return moduleId
      if (specifier === themesCssSpecifier) return cssId
      return undefined
    },
    load(id) {
      if (id !== moduleId && id !== cssId) return undefined
      this.addWatchFile(tokensFile(root))
      const json = readTokens(root)
      return id === moduleId ? tokensModule(json) : themesCss(json)
    },
  }
}
