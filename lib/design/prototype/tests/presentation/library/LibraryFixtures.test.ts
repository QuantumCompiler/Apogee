import { existsSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
// The rules alone, never the `?raw` imports: a fixture missing on disk fails its presets' tests here, not this file's load.
import { libraryPresetNames, libraryPresets } from '../../../source/presentation/library/Previews/LibraryWorldRules'
import { reviewPlaceNames, reviewPresets } from '../../../source/presentation/review/Previews/ReviewWorldRules'

const here = dirname(fileURLToPath(import.meta.url))
const prototype = resolve(here, '..', '..', '..')
const worlds = {
  library: resolve(prototype, 'source/presentation/library/Previews/LibraryWorlds.ts'),
  review: resolve(prototype, 'source/presentation/review/Previews/ReviewWorlds.ts'),
} as const

/** Every `?raw` import of a worlds file, by the file name a world names it by, with where it is on disk. */
function imported(worldsFile: string): Map<string, string> {
  const files = new Map<string, string>()
  for (const match of readFileSync(worldsFile, 'utf8').matchAll(/from '((?:@recorded|(?:\.\.\/)+fixtures)\/[^'?]+)\?raw'/g)) {
    const path = match[1] ?? ''
    const full = path.startsWith('@recorded/') ? resolve(prototype, '..', '..', 'src', 'darwin', 'tests', path.slice('@recorded/'.length)) : resolve(dirname(worldsFile), path)
    files.set(path.split('/').at(-1) ?? path, full)
  }
  return files
}

/** What is wrong with the files `preset` names: one no import holds, one missing on disk, or a design fixture not named `*-handmade*`, each naming the preset and the file. */
function problems(preset: string, names: readonly string[], files: ReadonlyMap<string, string>): string[] {
  return names.flatMap((name) => {
    const full = files.get(name)
    if (full === undefined) return [`${preset}: ${name} is no fixture its worlds file imports`]
    if (!existsSync(full)) return [`${preset}: ${name} is missing at ${full}`]
    if (full.includes('/lib/design/prototype/fixtures/') && !/-handmade\.[a-z]+$/.test(name)) return [`${preset}: ${name} is a design fixture not named *-handmade*`]
    if (readFileSync(full, 'utf8').length === 0) return [`${preset}: ${name} is empty`]
    return []
  })
}

describe('every file a Library or Review preset reads exists, and a design fixture says it is hand-made (design 1n)', () => {
  // The names are the rules' own: each file a preset's answers and folders look up, so a rule that reads another file is checked as it reads it.
  for (const preset of libraryPresets) {
    test(preset, () => expect(problems(preset, libraryPresetNames(preset), imported(worlds.library))).toEqual([]))
  }
  for (const preset of reviewPresets) {
    test(preset, () => expect(problems(preset, reviewPlaceNames(), imported(worlds.review))).toEqual([]))
  }

  test('the names are the rules’ own: every file the answers and folders look up, each once', () => {
    const library = libraryPresetNames('library:refused')
    expect(new Set(library).size).toBe(library.length)
    expect(library).toEqual(expect.arrayContaining(['agents-list.json', 'symphony-show.json', 'symphony-show-unknown-key.err', 'symphonies-notes-digest-handmade.yaml', 'mcp-presets.json', 'config-path-handmade.txt']))
    expect(reviewPlaceNames()).toEqual(expect.arrayContaining(['review-targets.json', 'review-skills.json', 'review-skill-show-back-end-handmade.json', 'review-run-dry.json', 'review-targets-add-gadget-handmade.txt', 'review-sweep.json']))
  })

  test('a planted preset naming a file nobody imports, or one gone from disk, fails naming the preset and the file', () => {
    const files = imported(worlds.library)
    const gone = resolve(prototype, 'fixtures/business/reads/agents-list-gone-handmade.json')
    files.set('agents-list-gone-handmade.json', gone)
    expect(problems('library:planted', ['agents-list.json', 'agents-list-invented.json', 'agents-list-gone-handmade.json'], files)).toEqual([
      'library:planted: agents-list-invented.json is no fixture its worlds file imports',
      `library:planted: agents-list-gone-handmade.json is missing at ${gone}`,
    ])
    const reviewFiles = imported(worlds.review)
    reviewFiles.set('review-skill-show-invented.json', resolve(prototype, 'fixtures/business/reads/review-skill-show-back-end-handmade.json'))
    expect(problems('review:planted', ['review-skill-show-invented.json'], reviewFiles)).toEqual(['review:planted: review-skill-show-invented.json is a design fixture not named *-handmade*'])
  })

  test("the five hand-made skill shows are review-skill-show.json's shape: its keys at every depth, a review.skill, the list's row for the lens", () => {
    const recorded = JSON.parse(readFileSync(resolve(prototype, '../../src/darwin/tests/business/reads/Fixtures/review-skill-show.json'), 'utf8')) as Record<string, unknown>
    const list = JSON.parse(readFileSync(resolve(prototype, '../../src/darwin/tests/business/reads/Fixtures/review-skills.json'), 'utf8')) as { data: Array<Record<string, unknown>> }
    const keys = (value: unknown, at = ''): string[] => (typeof value === 'object' && value !== null && !Array.isArray(value) ? Object.entries(value).flatMap(([key, inner]) => [`${at}${key}`, ...keys(inner, `${at}${key}.`)]) : [])
    for (const lens of ['back-end', 'concurrency', 'embedded', 'front-end', 'general-correctness']) {
      const show = JSON.parse(readFileSync(resolve(prototype, `fixtures/business/reads/review-skill-show-${lens}-handmade.json`), 'utf8')) as Record<string, unknown>
      expect(keys(show).sort(), lens).toEqual(keys(recorded).sort())
      expect(show.object).toBe('review.skill')
      const row = list.data.find((entry) => entry.name === lens)
      for (const key of ['applies', 'description', 'everywhere', 'source']) expect(show[key], `${lens}.${key}`).toEqual(row?.[key])
    }
  })
})
