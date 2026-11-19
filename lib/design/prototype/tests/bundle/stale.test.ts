import { cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterEach, describe, expect, test } from 'vitest'
import { builtHash, inputFiles, inputPath, recordingsNamed, sourcesHash, staleness } from '../../tooling/sources'

/** The prototype's root: this file is tests/bundle/stale.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
/** The committed build, lib/design/main.html. */
const built = resolve(root, '../main.html')

const scratch: string[] = []
afterEach(() => {
  for (const directory of scratch.splice(0)) rmSync(directory, { recursive: true, force: true })
})

/**
 * A copy of the build's inputs, to plant an edit in, laid out as the
 * repository is -- `lib/design/prototype/` beside `lib/src/darwin/tests/` --
 * so the recordings the source names are found where `@recorded` points.
 * Returns the copy's prototype root.
 */
function copyOfInputs(): string {
  const top = mkdtempSync(join(tmpdir(), 'apogee-design-stale-'))
  scratch.push(top)
  const copy = join(top, 'lib/design/prototype')
  for (const file of inputFiles(root)) {
    const to = inputPath(copy, file)
    mkdirSync(dirname(to), { recursive: true })
    cpSync(inputPath(root, file), to)
  }
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

describe('the two fixture roots in the hash (design 1d)', () => {
  test('from @recorded, exactly the recordings source/ names: no Swift test, no recording only a test reads', () => {
    const recorded = inputFiles(root).filter((file) => file.startsWith('@recorded/'))
    expect(recorded).toEqual(recordingsNamed(root))
    expect(recorded).toContain('@recorded/business/reads/Fixtures/agents-list-bad-config.err')
    expect(recorded.every((file) => file.includes('/Fixtures/'))).toBe(true)
    expect(recorded).not.toContain('@recorded/data/protocol/Fixtures/mock-actor.jsonl')
    expect(inputPath(root, '@recorded/business/reads/Fixtures/agents-list-bad-config.err')).toBe(
      resolve(root, '../../src/darwin/tests/business/reads/Fixtures/agents-list-bad-config.err'),
    )
  })

  test('a named recording re-recorded stales the build; a recording no source names, or a Swift test, does not', () => {
    const copy = copyOfInputs()
    const before = sourcesHash(copy)
    const tests = resolve(copy, '../../src/darwin/tests')
    mkdirSync(join(tests, 'presentation/monitor'), { recursive: true })
    writeFileSync(join(tests, 'presentation/monitor/MonitorViewModelTests.swift'), '// edited by another session\n')
    mkdirSync(join(tests, 'data/protocol/Fixtures'), { recursive: true })
    writeFileSync(join(tests, 'data/protocol/Fixtures/mock-actor.jsonl'), '{"type":"session"}\n')
    expect(sourcesHash(copy)).toBe(before)
    writeFileSync(join(tests, 'business/reads/Fixtures/agents-list-bad-config.err'), 'apogee agents: re-recorded\n')
    expect(sourcesHash(copy)).not.toBe(before)
  })

  test('a recording a source starts naming joins the inputs', () => {
    const copy = copyOfInputs()
    const before = inputFiles(copy)
    const preview = join(copy, 'source/presentation/monitor/Previews/MonitorPreset.ts')
    writeFileSync(preview, "import system from '@recorded/business/reads/Fixtures/suites-system.json?raw'\nexport const rules = [system]\n")
    const tests = resolve(copy, '../../src/darwin/tests/business/reads/Fixtures')
    writeFileSync(join(tests, 'suites-system.json'), readFileSync(resolve(recordedRootOf(root), 'business/reads/Fixtures/suites-system.json')))
    expect(inputFiles(copy)).toEqual([...before, '@recorded/business/reads/Fixtures/suites-system.json', 'source/presentation/monitor/Previews/MonitorPreset.ts'].sort())
  })

  test("every file under the design's own fixtures/ is an input", () => {
    const copy = copyOfInputs()
    const before = sourcesHash(copy)
    mkdirSync(join(copy, 'fixtures/business/reads'), { recursive: true })
    writeFileSync(join(copy, 'fixtures/business/reads/system-handmade.json'), '{}\n')
    expect(inputFiles(copy)).toContain('fixtures/business/reads/system-handmade.json')
    expect(sourcesHash(copy)).not.toBe(before)
  })
})

/** The Mac app's tests, from a prototype root. */
function recordedRootOf(prototype: string): string {
  return resolve(prototype, '../../src/darwin/tests')
}
