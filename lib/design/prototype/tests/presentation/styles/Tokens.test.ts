import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { tokens as generated } from '@tokens'
import { readTokens, themesCss, tokenCheck, tokensFile, tokensModule } from '../../../tooling/tokens'

/** The prototype's root: this file is tests/presentation/styles/Tokens.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')
const json = readTokens(root)

describe('the build step reads tokens.json (design 1e)', () => {
  test('it reads lib/design/spec/json/tokens.json, the one place a token is written', () => {
    expect(tokensFile(root)).toBe(resolve(root, '../spec/json/tokens.json'))
    expect(json).toEqual(JSON.parse(readFileSync(tokensFile(root), 'utf8')))
  })

  test('the typed tokens module is tokens.json, value for value', () => {
    expect(generated).toEqual(json)
    expect(tokensModule(json)).toContain(JSON.stringify(json))
  })
})

describe('the token check: the generated CSS equals tokens.json, value for value', () => {
  test('one block per theme holds its 25 colors, in tokens.json\'s letter case, and the shared block the rest', () => {
    const css = themesCss(json)
    expect(tokenCheck(css, json)).toEqual([])
    expect(css).toContain('[data-theme="dark"] {')
    expect(css).toContain('[data-theme="light"] {')
    expect(css).toContain('[data-theme="contrast"] {')
    expect(css).toContain('  --surface-window: #131415;')
    expect(css).toContain('  --media-caption: #FFFFFF;')
    expect(css).toContain('  --media-caption-fill: #000000;')
    expect(css).toContain('  --layout-sidebar: 256px;')
    expect(css).toContain('  --layout-monitor: 540px;')
    expect(css).toContain('  --scrim-modal: rgba(0,0,0,0.45);')
    expect(css).toContain('  --window-close: #FF5F57;')
    expect(css).toContain("  --font-ui: 'IBM Plex Sans', system-ui, sans-serif;")
    expect(css).toContain('  --t-body: 13px;')
    expect(css).toContain('  --motion-chevron: 150ms ease;')
    expect(css).toContain('  --motion-caret: 1.1s steps(1);')
    expect(css).toContain('  --motion-pulse: 1.4s ease-in-out;')
    // 75 colors, three color-schemes.
    expect(css.match(/^ {2}--[a-z]+-[a-z-]+: #[0-9A-F]{6};$/gm)?.length).toBeGreaterThanOrEqual(75)
    expect(css.match(/color-scheme: (dark|light);/g)).toEqual(['color-scheme: dark;', 'color-scheme: light;', 'color-scheme: dark;'])
  })

  test('a planted edit to the generated output fails it, naming the variable and both values', () => {
    const css = themesCss(json)
    const edited = css.replace('--surface-window: #131415;', '--surface-window: #131416;')
    expect(tokenCheck(edited, json)).toEqual([
      '[data-theme="dark"] --surface-window is #131416, and tokens.json says #131415',
    ])
    const dropped = css.replace('  --line-strong: #C6C6BF;\n', '')
    expect(tokenCheck(dropped, json)).toEqual(['[data-theme="light"] has no --line-strong: tokens.json says #C6C6BF'])
    const added = css.replace('[data-theme="contrast"] {\n', '[data-theme="contrast"] {\n  --surface-extra: #123456;\n')
    expect(tokenCheck(added, json)).toEqual(['[data-theme="contrast"] --surface-extra is not in tokens.json'])
    const shared = css.replace('--layout-sidebar: 256px;', '--layout-sidebar: 260px;')
    expect(tokenCheck(shared, json)).toEqual([':root --layout-sidebar is 260px, and tokens.json says 256px'])
    const motion = css.replace('--motion-chevron: 150ms ease;', '--motion-chevron: 200ms ease;')
    expect(tokenCheck(motion, json)).toEqual([':root --motion-chevron is 200ms ease, and tokens.json says 150ms ease'])
  })

  test("motion is each entry's leading duration and timing function, cut from tokens.json's words; words alone never reach the CSS", () => {
    const edited = (motion: Record<string, string>) => ({ ...json, motion: { ...json.motion, ...motion } })
    expect(themesCss(edited({ caret: '900ms linear, then a pause' }))).toContain('  --motion-caret: 900ms linear;')
    expect(themesCss(edited({ pulse: '.8s cubic-bezier(0.4, 0, 0.2, 1) glow' }))).toContain('  --motion-pulse: .8s cubic-bezier(0.4, 0, 0.2, 1);')
    expect(() => themesCss(edited({ pulse: 'about a second, gently' }))).toThrow(
      'tokens.json motion.pulse is "about a second, gently": a motion token starts with a CSS duration and timing function (1.4s ease-in-out)',
    )
    expect(() => themesCss(edited({ chevron: '150ms easy' }))).toThrow('tokens.json motion.chevron is "150ms easy"')
  })
})

describe('what this item added to tokens.json, from the frozen main.html', () => {
  test('the font stacks rootVars() writes, the window controls, the toast shadow and the monitor\'s width', () => {
    expect(json.type.fonts.stacks).toEqual({
      ui: {
        plex: "'IBM Plex Sans', system-ui, sans-serif",
        system: "-apple-system, BlinkMacSystemFont, 'SF Pro Text', 'Segoe UI Variable Text', system-ui, sans-serif",
        atkinson: "'Atkinson Hyperlegible', system-ui, sans-serif",
      },
      mono: {
        plex: "'IBM Plex Mono', ui-monospace, Menlo, monospace",
        system: "ui-monospace, 'SF Mono', 'Cascadia Mono', Menlo, monospace",
        jetbrains: "'JetBrains Mono', ui-monospace, Menlo, monospace",
      },
    })
    expect(json.fixed.windowControlColors).toEqual({ close: '#FF5F57', minimize: '#FEBC2E', zoom: '#28C840' })
    expect(json.fixed.toastShadow).toBe('0 8px 24px rgba(0,0,0,0.3)')
    expect(json.layoutPx.monitor).toBe(540)
    expect(json.layoutPx.narrowBelow).toBe(600)
  })
})

describe("the sizes the layout page showed that no token held (design 1r): tokens.json holds them, and the views that draw them read them", () => {
  const source = (path: string) => readFileSync(resolve(root, 'source/presentation', path), 'utf8')

  test('the two menus, a place in the sidebar and the focus ring are layout sizes, generated as their variables', () => {
    expect(json.layoutPx).toMatchObject({ menu: { thinking: 380, switchModel: 340 }, place: { width: 40, height: 34 }, focusRing: { width: 2, offset: 1 } })
    const css = themesCss(json)
    for (const line of ['--layout-menu-thinking: 380px;', '--layout-menu-switch-model: 340px;', '--layout-place-width: 40px;', '--layout-place-height: 34px;', '--layout-focus-ring-width: 2px;', '--layout-focus-ring-offset: 1px;']) {
      expect(css).toContain(`  ${line}`)
    }
  })

  test('the thinking and switch-model menus, the places and the focus ring are drawn at those variables, never at a number of their own', () => {
    const session = source('session/Views/SessionWindow.tsx')
    expect(session).toContain("aria-label=\"Thinking\" style={{ position: 'absolute', top: 38, right: 0, zIndex: 40, width: 'var(--layout-menu-thinking)' }}")
    expect(session).toContain("aria-label=\"Switch model\" style={{ position: 'absolute', top: 38, right: 0, zIndex: 40, width: 'var(--layout-menu-switch-model)' }}")
    const sidebar = source('shell/Views/Sidebar.tsx')
    expect(sidebar).toContain("width: 'var(--layout-place-width)',")
    expect(sidebar).toContain("height: 'var(--layout-place-height)',")
    expect(source('styles/Helmet.css')).toContain('{ outline: var(--layout-focus-ring-width) solid var(--status-accent); outline-offset: var(--layout-focus-ring-offset) }')
  })
})
