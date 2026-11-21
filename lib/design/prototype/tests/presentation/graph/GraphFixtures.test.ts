import { existsSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
// The rules alone, never the `?raw` imports: a fixture missing on disk fails its presets' tests here, not this file's load.
import { graphPlaceNames, graphPresets } from '../../../source/presentation/graph/Previews/GraphWorldRules'
import { knowledgePlaceNames, knowledgePresets } from '../../../source/presentation/knowledge/Previews/KnowledgeWorldRules'

const here = dirname(fileURLToPath(import.meta.url))
const prototype = resolve(here, '..', '..', '..')
const recorded = resolve(prototype, '..', '..', 'src', 'darwin', 'tests')
const worlds = {
  knowledge: resolve(prototype, 'source/presentation/knowledge/Previews/KnowledgeWorlds.ts'),
  graph: resolve(prototype, 'source/presentation/graph/Previews/GraphWorlds.ts'),
} as const

/** Every `?raw` import of a worlds file, by the file name a world names it by, with where it is on disk. */
function imported(worldsFile: string): Map<string, string> {
  const files = new Map<string, string>()
  for (const match of readFileSync(worldsFile, 'utf8').matchAll(/from '((?:@recorded|(?:\.\.\/)+fixtures)\/[^'?]+)\?raw'/g)) {
    const path = match[1] ?? ''
    const full = path.startsWith('@recorded/') ? resolve(recorded, path.slice('@recorded/'.length)) : resolve(dirname(worldsFile), path)
    files.set(path.split('/').at(-1) ?? path, full)
  }
  return files
}

/** What is wrong with the files `preset` names: one no import holds, one missing on disk, a design fixture not named `*-handmade*`, or an empty one, each naming the preset and the file. */
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

/** Every key path of a JSON value, arrays read through their first element. */
function keys(value: unknown, at = ''): string[] {
  if (Array.isArray(value)) return value.length === 0 ? [] : keys(value[0], `${at}[].`)
  if (typeof value !== 'object' || value === null) return []
  return Object.entries(value).flatMap(([key, inner]) => [`${at}${key}`, ...keys(inner, `${at}${key}.`)])
}

const design = (path: string) => readFileSync(resolve(prototype, 'fixtures', path), 'utf8')
const mac = (path: string) => readFileSync(resolve(recorded, path), 'utf8')

describe('every file a Knowledge or Code graph preset reads exists, and traces to a recording or a *-handmade* file (design 1o)', () => {
  for (const preset of knowledgePresets) test(preset, () => expect(problems(preset, knowledgePlaceNames(), imported(worlds.knowledge))).toEqual([]))
  for (const preset of graphPresets) test(preset, () => expect(problems(preset, graphPlaceNames(), imported(worlds.graph))).toEqual([]))

  test('the names are the rules’ own: every file the answers look up, each once, the hand-made ones among them', () => {
    expect(knowledgePlaceNames()).toEqual(['knowledge-list.json', 'collections-list-handmade.json', 'collection-info-knowledge-handmade.json', 'collection-info-vectors-mixed.json', 'collection-info.json', 'knowledge-query.json', 'knowledge-info-raw-1e2354-handmade.txt', 'collection-query.txt'])
    expect(graphPlaceNames()).toEqual(
      expect.arrayContaining(['stats.json', 'query.json', 'graph-files-handmade.json', 'graph-explain-gone-fetch-handmade.err', 'explain.json', 'graph-prose-report.json', 'graph-delete.txt', 'graph-delete-notes-handmade.txt', 'graph-update-demo-handmade.txt', 'graph-dedupe-merged-handmade.txt']),
    )
    expect(new Set(graphPlaceNames()).size).toBe(graphPlaceNames().length)
  })

  test('a planted preset naming a file nobody imports, or one gone from disk, fails naming the preset and the file', () => {
    const files = imported(worlds.graph)
    const gone = resolve(prototype, 'fixtures/business/reads/graph-gone-handmade.json')
    files.set('graph-gone-handmade.json', gone)
    files.set('graph-invented.json', resolve(prototype, 'fixtures/business/reads/graph-files-handmade.json'))
    expect(problems('graph:planted', ['stats.json', 'graph-nobody.json', 'graph-gone-handmade.json', 'graph-invented.json'], files)).toEqual([
      'graph:planted: graph-nobody.json is no fixture its worlds file imports',
      `graph:planted: graph-gone-handmade.json is missing at ${gone}`,
      'graph:planted: graph-invented.json is a design fixture not named *-handmade*',
    ])
  })

  test("the hand-made Files picture is graph query's shape: query.json's keys at every depth, files only, its imports between them, the one recorded imports edge among them", () => {
    const picture = JSON.parse(design('business/reads/graph-files-handmade.json')) as { entities: Array<{ type: string; name: string }>; relations: Array<{ from: string; to: string; relation: string }> }
    const query = JSON.parse(mac('presentation/graph/Fixtures/query.json')) as typeof picture
    expect([...new Set(keys(picture))].sort()).toEqual([...new Set(keys(query))].sort())
    expect(picture.entities.every((entity) => entity.type === 'file')).toBe(true)
    expect(picture.relations.every((relation) => relation.relation === 'imports')).toBe(true)
    expect(picture.relations).toEqual(expect.arrayContaining(query.relations.filter((relation) => relation.relation === 'imports')))
    const named = new Set(picture.entities.map((entity) => entity.name))
    expect(query.entities.filter((entity) => entity.type === 'file').every((entity) => named.has(entity.name)), 'every file the recorded picture holds').toBe(true)
  })

  test('the hand-made lines are the recordings’ shapes: the raw conversation, the refusal, the collection’s info, the merged list and the rebuild’s line', () => {
    const raw = design('business/reads/knowledge-info-raw-1e2354-handmade.txt').split('\n')
    const recordedRaw = mac('business/reads/Fixtures/knowledge-info-raw.txt').split('\n')
    expect(raw.map((line) => line.split(':')[0])).toEqual(recordedRaw.map((line) => line.split(':')[0]))
    expect(raw.at(-2)).toBe(recordedRaw.at(-2))
    const refusal = design('business/reads/graph-explain-gone-fetch-handmade.err')
    const recordedRefusal = mac('business/reads/Fixtures/graph-explain-gone.err')
    const shape = /^apogee graph: no node named '[^']+' in graph '[^']+' -- near matches: ([a-z]+:[^ ]+ \([^)]+\)(, )?){5}\n$/
    expect([shape.test(refusal), shape.test(recordedRefusal)]).toEqual([true, true])
    expect(keys(JSON.parse(design('business/reads/collection-info-knowledge-handmade.json'))).sort()).toEqual(keys(JSON.parse(mac('business/reads/Fixtures/collection-info.json'))).sort())
    // The rebuild's line: graph.cpp's `print_code_summary` Code graph line, over stats.json's counts, its sync suffix every node and edge added (the deleted graph built again).
    const stats = JSON.parse(mac('presentation/graph/Fixtures/stats.json')) as { nodes: number; edges: number }
    expect(design('business/jobs/graph-update-demo-handmade.txt')).toBe(`  Code graph:       ${stats.nodes} node(s), ${stats.edges} edge(s) (nodes +${stats.nodes} ~0 -0; edges +${stats.edges} -0)\n`)
    const merged = JSON.parse(design('business/reads/collections-list-handmade.json')) as { data: unknown[] }
    const rows = [...(JSON.parse(mac('business/reads/Fixtures/collections-list.json')) as { data: Array<{ name: string }> }).data, ...(JSON.parse(mac('business/reads/Fixtures/collections-list-vectors.json')) as { data: Array<{ name: string }> }).data.filter((row) => row.name === 'mixed')]
    expect(merged.data).toEqual([...rows].sort((a, b) => a.name.localeCompare(b.name)))
  })
})
