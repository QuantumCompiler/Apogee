import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterEach, describe, expect, test } from 'vitest'
import { builtHash, inputFiles, sourcesHash, staleness } from '../../tooling/sources'

/** The prototype's root: this file is tests/bundle/stale.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
/** The committed build, lib/design/main.html. */
const built = resolve(root, '../main.html')

const scratch: string[] = []
afterEach(() => {
  for (const directory of scratch.splice(0)) rmSync(directory, { recursive: true, force: true })
})

/** A copy of the build's inputs, to plant an edit in. */
function copyOfInputs(): string {
  const copy = mkdtempSync(join(tmpdir(), 'apogee-design-stale-'))
  scratch.push(copy)
  for (const file of inputFiles(root)) cpSync(join(root, file), join(copy, file))
  return copy
}

describe('the staleness check', () => {
  test('lib/design/main.html is built from the sources as they are', () => {
    const problem = staleness(readFileSync(built, 'utf8'), root)
    if (problem) throw new Error(problem)
  })

  test('a planted one-character source edit fails it, naming main.html and npm run build', () => {
    const copy = copyOfInputs()
    const html = `<!doctype html><html><head><meta name="apogee-design-sources" content="${sourcesHash(copy)}"></head></html>`
    expect(staleness(html, copy)).toBeUndefined()
    const file = join(copy, 'source/presentation/app/ViewModels/AppViewModel.ts')
    writeFileSync(file, readFileSync(file, 'utf8').replace("'Apogee'", "'Apogeo'"))
    expect(staleness(html, copy)).toBe(
      `lib/design/main.html is stale: it was built from sources ${builtHash(html)}, and they are now ${sourcesHash(copy)}. ` +
        'Run npm run build in lib/design/prototype, and commit the new main.html',
    )
  })

  test('a planted stale main.html fails it, and so does one npm run build did not write', () => {
    const html = readFileSync(built, 'utf8')
    const planted = html.replace(/sha256-[0-9a-f]{64}/, `sha256-${'0'.repeat(64)}`)
    expect(staleness(planted, root)).toBe(
      `lib/design/main.html is stale: it was built from sources sha256-${'0'.repeat(64)}, and they are now ${sourcesHash(root)}. ` +
        'Run npm run build in lib/design/prototype, and commit the new main.html',
    )
    expect(staleness('<!doctype html><html><head></head><body></body></html>', root)).toBe(
      'lib/design/main.html carries no apogee-design-sources hash, so npm run build did not write it. ' +
        'Run npm run build in lib/design/prototype, and commit the new main.html',
    )
  })

  test("the hash covers the build's inputs, sorted by path, and nothing else", () => {
    const files = inputFiles(root)
    expect(files).toEqual([...files].sort())
    for (const input of ['main.html', 'package.json', 'package-lock.json', 'tsconfig.json', 'vite.config.ts', 'source/main.tsx']) {
      expect(files).toContain(input)
    }
    expect(files.some((file) => file.startsWith('tooling/'))).toBe(true)
    expect(files.filter((file) => /^(tests|e2e|node_modules|dist)\/|^(modules\.json|vitest\.config\.ts|playwright\.config\.ts)$/.test(file))).toEqual([])
    const copy = copyOfInputs()
    expect(sourcesHash(copy)).toBe(sourcesHash(root))
  })
})
