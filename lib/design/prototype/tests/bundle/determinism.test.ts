import { spawnSync } from 'node:child_process'
import { mkdtempSync, readdirSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterAll, describe, expect, test } from 'vitest'

/** The prototype's root: this file is tests/bundle/determinism.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const scratch = mkdtempSync(join(tmpdir(), 'apogee-design-build-'))
afterAll(() => rmSync(scratch, { recursive: true, force: true }))

/**
 * One production build of the tree as it is, written to the scratch folder
 * (APOGEE_DESIGN_OUT), never over the committed lib/design/main.html. It runs
 * as npm run build does, in its own process, with no NODE_ENV from the test
 * runner.
 */
function build(name: string): { html: Buffer; emitted: string[] } {
  const outDir = join(scratch, `${name}-dist`)
  const destination = join(scratch, `${name}.html`)
  const environment: NodeJS.ProcessEnv = { ...process.env, APOGEE_DESIGN_OUT: destination }
  delete environment.NODE_ENV
  delete environment.VITEST
  const run = spawnSync(
    process.execPath,
    [join(root, 'node_modules/vite/bin/vite.js'), 'build', '--outDir', outDir, '--emptyOutDir', '--logLevel', 'error'],
    { cwd: root, env: environment, encoding: 'utf8' },
  )
  if (run.status !== 0) throw new Error(`the build failed (${run.status}):\n${run.stdout}\n${run.stderr}`)
  return { html: readFileSync(destination), emitted: readdirSync(outDir) }
}

describe('the build is deterministic', () => {
  test('two builds from the same tree are byte-identical, each one file', () => {
    const first = build('first')
    const second = build('second')
    expect(first.emitted).toEqual(['main.html'])
    expect(second.emitted).toEqual(['main.html'])
    expect(first.html.length).toBeGreaterThan(0)
    expect(first.html.equals(second.html)).toBe(true)
  })
})
