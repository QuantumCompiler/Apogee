import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { readTokens } from '../../../tooling/tokens'
import { DiskTree, PlantedTree } from '../layering/SourceTree'
import { TokenScan } from './TokenScan'

/** The prototype's root: this file is tests/architecture/tokens/tokens.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')
const scan = TokenScan.from(readTokens(root))

describe('the token scan, on this prototype (design 1e)', () => {
  test('no color, type size, font or motion is written in source/ but through tokens.json', () => {
    const found = scan.violations(new DiskTree(root))
    expect(found, found.join('\n')).toEqual([])
  })

  test('the fixed values it allows are tokens.json\'s: the scrim, the shadows and the window controls', () => {
    expect([...scan.fixed].sort()).toEqual(['#28c840', '#febc2e', '#ff5f57', 'rgba(0,0,0,0.3)', 'rgba(0,0,0,0.35)', 'rgba(0,0,0,0.45)'])
    expect(scan.families).toEqual([
      'IBM Plex Sans', 'SF Pro Text', 'Segoe UI Variable Text', 'Atkinson Hyperlegible', 'IBM Plex Mono', 'SF Mono', 'Cascadia Mono', 'JetBrains Mono',
    ])
  })
})

/** A view and a stylesheet, clean, that each test plants one thing into. */
function planted(view: string, css = '.ap .x { color: var(--text-primary) }\n'): PlantedTree {
  return new PlantedTree({
    'source/presentation/monitor/Views/MonitorView.tsx': view,
    'source/presentation/styles/Helmet.css': css,
  })
}

const cleanView =
  "export const MonitorView = () => <section style={{ color: 'var(--text-secondary)', fontSize: 'var(--t-small)', fontFamily: 'var(--font-mono)' }} aria-labelledby=\"cf-h\">#root</section>\n"

describe('the token scan, on planted values', () => {
  test('a clean view and stylesheet pass', () => {
    expect(scan.violations(planted(cleanView))).toEqual([])
  })

  test('a planted #fff in a view fails, naming the file', () => {
    const view = cleanView.replace("color: 'var(--text-secondary)'", "color: '#fff'")
    expect(scan.violations(planted(view))).toEqual([
      'source/presentation/monitor/Views/MonitorView.tsx writes the color #fff: every color is a var(--…) from tokens.json, and only its fixed values are written as they are',
    ])
  })

  test('a hex or a color function in a stylesheet fails; a hex in a comment does not', () => {
    const css = '/* the old #131415 */\n.ap .x { color: #FFFFFF; background: rgb(1, 2, 3); border-color: hsl(0 0% 50%) }\n'
    expect(scan.violations(planted(cleanView, css))).toEqual([
      'source/presentation/styles/Helmet.css writes the color #FFFFFF: every color is a var(--…) from tokens.json, and only its fixed values are written as they are',
      'source/presentation/styles/Helmet.css writes the color rgb(1, 2, 3): every color is a var(--…) from tokens.json, and only its fixed values are written as they are',
      'source/presentation/styles/Helmet.css writes the color hsl(0 0% 50%): every color is a var(--…) from tokens.json, and only its fixed values are written as they are',
    ])
  })

  test('the fixed values are the only exceptions: the window controls, the scrim and the shadows, in either spelling', () => {
    const view = cleanView.replace("color: 'var(--text-secondary)'", "background: '#FF5F57', boxShadow: '0 8px 24px rgba(0,0,0,.3)'")
    const css = '.ap .scrim { background: rgba(0, 0, 0, 0.45) }\n.ap .lights { background: #28c840 }\n'
    expect(scan.violations(planted(view, css))).toEqual([])
  })

  test('a type size not from var(--t-…) fails, in a view or a stylesheet; a relative em does not', () => {
    const view = cleanView.replace("fontSize: 'var(--t-small)'", 'fontSize: 13')
    const css = '.ap code { font-size: .86em }\n.ap .x { font-size: 12px }\n'
    expect(scan.violations(planted(view, css))).toEqual([
      'source/presentation/monitor/Views/MonitorView.tsx sets a font size of 13: type sizes are var(--t-…) from tokens.json',
      'source/presentation/styles/Helmet.css sets a font size of 12px: type sizes are var(--t-…) from tokens.json',
    ])
  })

  test('a motion written as a time, in a view or a stylesheet, fails; motion from var(--motion-…) does not', () => {
    const view = cleanView.replace("color: 'var(--text-secondary)'", "transition: 'transform .15s ease', animation: 'x var(--motion-pulse) infinite'")
    const css =
      '.ap .x { animation: x 1s ease }\n.ap .y { transition-duration: 150ms }\n' +
      '.ap .caret { animation: ap-blink var(--motion-caret) infinite }\n.ap .z { transition: transform var(--motion-chevron) }\n'
    expect(scan.violations(planted(view, css))).toEqual([
      'source/presentation/monitor/Views/MonitorView.tsx writes the motion transform .15s ease: motion is var(--motion-…) from tokens.json',
      'source/presentation/styles/Helmet.css writes the motion x 1s ease: motion is var(--motion-…) from tokens.json',
      'source/presentation/styles/Helmet.css writes the motion 150ms: motion is var(--motion-…) from tokens.json',
    ])
  })

  test('a font not from var(--font-ui) or var(--font-mono) fails; inherit, and a face tokens.json names, do not', () => {
    const view = cleanView.replace("fontFamily: 'var(--font-mono)'", "fontFamily: 'Helvetica'")
    const css =
      ".ap button { font-family: inherit }\n.ap .x { font-family: 'Menlo', monospace }\n" +
      "@font-face { font-family: 'IBM Plex Sans'; font-weight: 400; src: url(./a.woff2) format('woff2') }\n" +
      "@font-face { font-family: 'Comic Sans'; src: url(./b.woff2) format('woff2') }\n"
    expect(scan.violations(planted(view, css))).toEqual([
      'source/presentation/monitor/Views/MonitorView.tsx sets the font Helvetica: fonts are var(--font-ui) and var(--font-mono) from tokens.json',
      "source/presentation/styles/Helmet.css sets the font 'Menlo', monospace: fonts are var(--font-ui) and var(--font-mono) from tokens.json",
      "source/presentation/styles/Helmet.css declares the font face 'Comic Sans', which no font stack in tokens.json names",
    ])
  })
})

describe("the code colors (design 1u): the lexer's five are tokens.json's code.* variables", () => {
  test("a code color written as its value in the lexer fails, naming the file; the real lexer's are var(--code-…)", () => {
    const lexer = "export const color = (kind: string) => (kind === 'keyword' ? 'var(--code-keyword)' : 'var(--code-string)')\n"
    expect(scan.violations(new PlantedTree({ 'source/presentation/styles/Components/CodeLexer.ts': lexer }))).toEqual([])
    const written = lexer.replace("'var(--code-keyword)'", "'#C792EA'")
    expect(scan.violations(new PlantedTree({ 'source/presentation/styles/Components/CodeLexer.ts': written }))).toEqual([
      'source/presentation/styles/Components/CodeLexer.ts writes the color #C792EA: every color is a var(--…) from tokens.json, and only its fixed values are written as they are',
    ])
  })
})

describe('the Build pages (design 1r): the token scan covers them, so no hex is left in them', () => {
  const gallery = 'source/presentation/gallery'
  const processes = `${gallery}/Views/BuildProcessesPage.tsx`

  test('the three pages, their data and their view-models are scanned, and are clean', () => {
    const tree = new DiskTree(root)
    const files = tree.files('source')
    for (const page of ['BuildContractPage', 'BuildLayoutPage', 'BuildProcessesPage']) expect(files).toContain(`${gallery}/Views/${page}.tsx`)
    for (const model of ['contract', 'layout', 'nativeControls', 'processes']) expect(files).toContain(`${gallery}/Models/${model}.ts`)
    for (const viewModel of ['BuildContractViewModel', 'BuildLayoutViewModel', 'BuildProcessesViewModel']) expect(files).toContain(`${gallery}/ViewModels/${viewModel}.ts`)
    expect(scan.violations(tree).filter((found) => found.startsWith(gallery))).toEqual([])
  })

  test("a planted #938F86, the old board's connector gray, in the processes view fails, naming it", () => {
    const source = readFileSync(resolve(root, processes), 'utf8')
    const planted = source.replace('color: paint(face.connector)', "color: '#938F86'")
    expect(planted).not.toBe(source)
    expect(scan.violations(new PlantedTree({ [processes]: source }))).toEqual([])
    expect(scan.violations(new PlantedTree({ [processes]: planted }))).toEqual([
      `${processes} writes the color #938F86: every color is a var(--…) from tokens.json, and only its fixed values are written as they are`,
    ])
  })
})
