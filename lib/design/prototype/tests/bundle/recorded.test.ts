import { mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { runInNewContext } from 'node:vm'
import { build } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'
import { afterAll, describe, expect, test } from 'vitest'
import { recordedAlias, recordedRoot } from '../../tooling/sources'

/** The prototype's root: this file is tests/bundle/recorded.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const scratch = mkdtempSync(join(tmpdir(), 'apogee-design-recorded-'))
afterAll(() => rmSync(scratch, { recursive: true, force: true }))

describe('a recording in the built file (design 1d)', () => {
  test('imported by name through @recorded with ?raw, it is inlined into the one file, byte for byte', async () => {
    const recording = 'business/reads/Fixtures/agents-list-bad-config.err'
    const page = join(scratch, 'page')
    const outDir = join(scratch, 'dist')
    mkdirSync(page, { recursive: true })
    // A planted page, built with the app's alias and its one-file plugin: its script holds the recording and nothing else.
    writeFileSync(
      join(page, 'main.html'),
      '<!doctype html><html><head><title>t</title></head><body><script type="module" src="./entry.ts"></script></body></html>\n',
    )
    writeFileSync(
      join(page, 'entry.ts'),
      `import text from '${recordedAlias}/${recording}?raw'\n;(globalThis as { recorded?: string }).recorded = text\n`,
    )
    await build({
      configFile: false,
      root: page,
      logLevel: 'silent',
      publicDir: false,
      resolve: { alias: { [recordedAlias]: recordedRoot(root) } },
      plugins: [viteSingleFile()],
      build: { outDir, emptyOutDir: true, modulePreload: { polyfill: false }, rolldownOptions: { input: join(page, 'main.html') } },
    })
    expect(readdirSync(outDir)).toEqual(['main.html'])
    const html = readFileSync(join(outDir, 'main.html'), 'utf8')
    const script = /<script type="module"[^>]*>([\s\S]*?)<\/script>/.exec(html)?.[1]
    expect(script).toBeDefined()
    const sandbox: { recorded?: string } = {}
    runInNewContext(script ?? '', sandbox)
    expect(sandbox.recorded).toBe(readFileSync(join(recordedRoot(root), recording), 'utf8'))
  })
})
