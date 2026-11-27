import { posix } from 'node:path'
import ts from 'typescript'
import type { TokensJson } from '../../../tooling/tokens'
import type { SourceTree } from '../layering/SourceTree'

/**
 * The token scan (design 1e): the design's rule "every color is a token" (the
 * design's ADR 0001, §7's third rule, whose Enforcement asks an item that can
 * check a rule mechanically to add the check and name the record) held
 * mechanically, with its siblings -- "type sizes are var(--t-…) and fonts
 * var(--font-ui) and var(--font-mono)", and motion var(--motion-…) -- over
 * every view, view-model and stylesheet in `source/`. tokens.json is the only
 * place a color, type size, font or motion is written; the views use its
 * variables. The fixed values (the modal scrim, the shadows, the window
 * controls) are the only colors that may be written as they are, and only as
 * tokens.json writes them.
 *
 * What it reads: in TypeScript, every string, template and JSX text (never a
 * comment), and each `fontSize`, `fontFamily`, `transition…` and
 * `animation…` an object literal sets; in a stylesheet, every declaration
 * (never a comment or a selector), and each `@font-face`'s family, which must
 * be one tokens.json's font stacks name. A transition or animation that
 * writes a time (`.15s`, `150ms`) fails: its duration and timing function are
 * a `var(--motion-…)`.
 */
export class TokenScan {
  /** A hex color: 3, 4, 6 or 8 digits, not followed by more of a name (`#cf-h` and `#root` are ids, not colors). */
  static readonly hex = /#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{4}|[0-9a-fA-F]{3})(?![0-9a-zA-Z_-])/g

  /** A color function. */
  static readonly colorFunction = /\b(?:rgba?|hsla?|hwb|lab|lch|oklab|oklch)\([^)]*\)/g

  private constructor(
    /** The fixed values' colors, normalized: lower case, no spaces, `.45` as `0.45`. */
    readonly fixed: ReadonlySet<string>,
    /** Every family a font stack names, in tokens.json's order. */
    readonly families: readonly string[],
  ) {}

  /** The scan tokens.json allows: its fixed values, and the families its stacks name. */
  static from(tokens: TokensJson): TokenScan {
    const fixed = new Set<string>()
    const walk = (value: unknown): void => {
      if (typeof value === 'string') {
        for (const match of value.matchAll(TokenScan.hex)) fixed.add(TokenScan.normalized(match[0]))
        for (const match of value.matchAll(TokenScan.colorFunction)) fixed.add(TokenScan.normalized(match[0]))
      } else if (value && typeof value === 'object') {
        for (const inner of Object.values(value)) walk(inner)
      }
    }
    walk(tokens.fixed)
    const families: string[] = []
    for (const stack of [...Object.values(tokens.type.fonts.stacks.ui), ...Object.values(tokens.type.fonts.stacks.mono)]) {
      for (const match of stack.matchAll(/'([^']+)'/g)) {
        const name = match[1]
        if (name !== undefined && !families.includes(name)) families.push(name)
      }
    }
    return new TokenScan(fixed, families)
  }

  /** A color as compared: `rgba(0, 0, 0, .45)` and `rgba(0,0,0,0.45)` are one. */
  static normalized(color: string): string {
    return color.toLowerCase().replace(/\s+/g, '').replace(/([(,])\./g, '$10.')
  }

  /** Every value `source/` writes that should be a token, each a sentence, in file order. */
  violations(tree: SourceTree): string[] {
    const found: string[] = []
    for (const file of tree.files('source')) {
      const text = tree.contents(file) ?? ''
      const extension = posix.extname(file)
      if (extension === '.css') found.push(...this.stylesheet(file, text))
      else if (['.ts', '.tsx'].includes(extension) && !file.endsWith('.d.ts')) found.push(...this.code(file, text))
    }
    return found
  }

  /** The colors a piece of text writes that are not fixed values. */
  private colors(file: string, text: string): string[] {
    const found: string[] = []
    const matches = [...text.matchAll(TokenScan.hex), ...text.matchAll(TokenScan.colorFunction)].sort((a, b) => (a.index ?? 0) - (b.index ?? 0))
    for (const match of matches) {
      if (this.fixed.has(TokenScan.normalized(match[0]))) continue
      found.push(`${file} writes the color ${match[0]}: every color is a var(--…) from tokens.json, and only its fixed values are written as they are`)
    }
    return found
  }

  private static fontSize(file: string, value: string): string[] {
    const ok = /^var\(--t-[a-z0-9-]+\)$/.test(value) || /^[\d.]+(em|%)$/.test(value) || value === 'inherit'
    return ok ? [] : [`${file} sets a font size of ${value}: type sizes are var(--t-…) from tokens.json`]
  }

  private static fontFamily(file: string, value: string): string[] {
    const ok = value === 'var(--font-ui)' || value === 'var(--font-mono)' || value === 'inherit'
    return ok ? [] : [`${file} sets the font ${value}: fonts are var(--font-ui) and var(--font-mono) from tokens.json`]
  }

  /** A time: `1s`, `.15s`, `150ms`, at the start or after a space, a comma or a parenthesis. */
  static readonly time = /(^|[\s,(])\d*\.?\d+m?s\b/

  /** A transition or animation property, in CSS (`transition-duration`) or in a style object (`transitionDuration`). */
  static isMotion(property: string): boolean {
    return /^(transition|animation)([-A-Z]|$)/.test(property)
  }

  private static motion(file: string, value: string): string[] {
    return TokenScan.time.test(value) ? [`${file} writes the motion ${value}: motion is var(--motion-…) from tokens.json`] : []
  }

  /** A stylesheet: its declarations, outside comments; each @font-face's family. */
  private stylesheet(file: string, source: string): string[] {
    const found: string[] = []
    const text = source.replace(/\/\*[\s\S]*?\*\//g, '')
    for (const block of text.matchAll(/(@font-face\s*)?\{([^{}]*)\}/g)) {
      const fontFace = block[1] !== undefined
      for (const declaration of (block[2] ?? '').split(';')) {
        const colon = declaration.indexOf(':')
        if (colon < 0) continue
        const property = declaration.slice(0, colon).trim().toLowerCase()
        const value = declaration.slice(colon + 1).trim()
        if (fontFace && property === 'font-family') {
          const name = value.replace(/^['"]|['"]$/g, '')
          if (!this.families.includes(name)) found.push(`${file} declares the font face ${value}, which no font stack in tokens.json names`)
          continue
        }
        if (fontFace) continue
        found.push(...this.colors(file, value))
        if (property === 'font-size') found.push(...TokenScan.fontSize(file, value))
        if (property === 'font-family' || property === 'font') found.push(...TokenScan.fontFamily(file, value))
        if (TokenScan.isMotion(property)) found.push(...TokenScan.motion(file, value))
      }
    }
    return found
  }

  /** TypeScript: every string-like literal's colors, and each literal `fontSize` and `fontFamily`. */
  private code(file: string, source: string): string[] {
    const found: string[] = []
    const kind = file.endsWith('.tsx') ? ts.ScriptKind.TSX : ts.ScriptKind.TS
    const tree = ts.createSourceFile(file, source, ts.ScriptTarget.Latest, true, kind)
    const visit = (node: ts.Node): void => {
      if (ts.isPropertyAssignment(node) && (ts.isIdentifier(node.name) || ts.isStringLiteral(node.name))) {
        const name = node.name.text
        const value = node.initializer
        const literal = ts.isStringLiteral(value) || ts.isNoSubstitutionTemplateLiteral(value) ? value.text : ts.isNumericLiteral(value) ? value.text : undefined
        if (literal !== undefined && (name === 'fontSize' || name === 'font-size')) found.push(...TokenScan.fontSize(file, literal))
        if (literal !== undefined && (name === 'fontFamily' || name === 'font-family')) found.push(...TokenScan.fontFamily(file, literal))
        if (literal !== undefined && TokenScan.isMotion(name)) found.push(...TokenScan.motion(file, literal))
      }
      if (
        ts.isStringLiteral(node) ||
        ts.isNoSubstitutionTemplateLiteral(node) ||
        ts.isTemplateHead(node) ||
        ts.isTemplateMiddle(node) ||
        ts.isTemplateTail(node) ||
        ts.isJsxText(node)
      ) {
        found.push(...this.colors(file, node.text))
      }
      ts.forEachChild(node, visit)
    }
    visit(tree)
    return found
  }
}
