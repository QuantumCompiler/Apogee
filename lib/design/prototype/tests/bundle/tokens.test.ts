import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { readTokens, tokenCheck } from '../../tooling/tokens'

/** The prototype's root: this file is tests/bundle/tokens.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
/** The committed build, lib/design/main.html. */
const built = readFileSync(resolve(root, '../main.html'), 'utf8')

/** The stylesheets the built file carries inline. */
function styles(html: string): string {
  return [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)].map((match) => match[1] ?? '').join('\n')
}

/** Every url() the stylesheets name that is not a data: URI: what would load another file. */
function loads(css: string): string[] {
  return [...css.matchAll(/url\(\s*(['"]?)([^'")]*)\1\s*\)/g)].map((match) => match[2] ?? '').filter((url) => !url.startsWith('data:'))
}

describe('the built file carries the tokens (design 1e)', () => {
  test("its CSS custom properties are tokens.json's, value for value", () => {
    expect(tokenCheck(styles(built), readTokens(root))).toEqual([])
  })

  test('a planted edit to the built output fails the token check', () => {
    const planted = styles(built).replace('--status-accent: #7FD4C1;', '--status-accent: #7FD4C2;')
    expect(tokenCheck(planted, readTokens(root))).toEqual(['[data-theme="dark"] --status-accent is #7FD4C2, and tokens.json says #7FD4C1'])
  })

  test('its ten web fonts are inlined: every url() is a data: URI, and the faces are the old fonts link\'s', () => {
    const css = styles(built)
    expect(loads(css)).toEqual([])
    expect(loads(".x { src: url('./files/a.woff2') }")).toEqual(['./files/a.woff2'])
    const faces = [...css.matchAll(/@font-face\s*\{[^}]*font-family:\s*'([^']+)';[^}]*font-weight:\s*(\d+);[^}]*src:\s*url\(['"]?data:font\/woff2;base64,/g)].map(
      (match) => `${match[1]} ${match[2]}`,
    )
    expect(faces).toEqual([
      'IBM Plex Sans 400', 'IBM Plex Sans 500', 'IBM Plex Sans 600',
      'IBM Plex Mono 400', 'IBM Plex Mono 500', 'IBM Plex Mono 600',
      'Atkinson Hyperlegible 400', 'Atkinson Hyperlegible 700',
      'JetBrains Mono 400', 'JetBrains Mono 500',
    ])
  })
})
