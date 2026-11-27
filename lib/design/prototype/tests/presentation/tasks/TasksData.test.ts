import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import ts from 'typescript'
import { describe, expect, test } from 'vitest'
// The rules alone, never the `?raw` imports: a fixture missing on disk fails its presets' tests here, not this file's load.
import { suitesPlaceNames, suitesPresets } from '../../../source/presentation/suites/Previews/SuitesWorldRules'
import { tasksPlaceNames, tasksPresets } from '../../../source/presentation/tasks/Previews/TasksWorldRules'

/**
 * Design 1p's data tests: no task or suite the CLI recorded is written into
 * a view or view-model of Suites or Tasks -- every row comes from a
 * recording or a `*-handmade*` file, through a preset's `Previews/` world --
 * and every file a Suites or Tasks preset names exists and is one.
 */
const here = dirname(fileURLToPath(import.meta.url))
const prototype = resolve(here, '..', '..', '..')
const recorded = resolve(prototype, '..', '..', 'src', 'darwin', 'tests')
const worlds = {
  suites: resolve(prototype, 'source/presentation/suites/Previews/SuitesWorlds.ts'),
  tasks: resolve(prototype, 'source/presentation/tasks/Previews/TasksWorlds.ts'),
} as const

/** The tasks and suites the recordings hold: each task's goal and id, each suite's name. */
function recordedData(): { readonly goals: readonly string[]; readonly ids: readonly string[]; readonly suites: readonly string[] } {
  const lists = ['task-list-all.json', 'task-list-killed.json', 'task-list-interrupted.json'].map((file) => JSON.parse(readFileSync(resolve(recorded, 'presentation/tasks/Fixtures', file), 'utf8')) as { data: Array<{ goal: string; id: string }> })
  const statuses = readdirSync(resolve(recorded, 'presentation/tasks/Fixtures'))
    .filter((file) => file.startsWith('task-status') && file.endsWith('.json'))
    .map((file) => JSON.parse(readFileSync(resolve(recorded, 'presentation/tasks/Fixtures', file), 'utf8')) as { goal: string; id: string })
  const rows = [...lists.flatMap((list) => list.data), ...statuses]
  const suites = readFileSync(resolve(recorded, 'business/reads/Fixtures/suites-config-get.txt'), 'utf8').split(/\s+/).filter(Boolean)
  return { goals: [...new Set(rows.map((row) => row.goal))], ids: [...new Set(rows.map((row) => row.id))], suites }
}

/** Every string a file writes -- literals, templates' parts and JSX text, never a comment. */
function strings(file: string, source: string): string[] {
  const found: string[] = []
  const tree = ts.createSourceFile(file, source, ts.ScriptTarget.Latest, true, file.endsWith('.tsx') ? ts.ScriptKind.TSX : ts.ScriptKind.TS)
  const visit = (node: ts.Node): void => {
    if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node) || ts.isTemplateHead(node) || ts.isTemplateMiddle(node) || ts.isTemplateTail(node) || ts.isJsxText(node)) found.push(node.text)
    ts.forEachChild(node, visit)
  }
  visit(tree)
  return found
}

/** What a view or view-model writes of the recorded data, each naming its file and the datum. */
function written(files: ReadonlyMap<string, string>): string[] {
  const data = recordedData()
  const escape = (word: string) => word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  const patterns = [...data.goals, ...data.ids, ...data.suites].map((datum) => [datum, new RegExp(`(^|[^A-Za-z0-9_-])${escape(datum)}($|[^A-Za-z0-9_-])`)] as const)
  return [...files.entries()].flatMap(([file, source]) => strings(file, source).flatMap((text) => patterns.filter(([, pattern]) => pattern.test(text)).map(([datum]) => `${file} writes the recorded ${JSON.stringify(datum)}`)))
}

/** The views and view-models of Suites and Tasks, by their path in the prototype. */
function placeFiles(): Map<string, string> {
  const files = new Map<string, string>()
  const walk = (directory: string): void => {
    for (const name of readdirSync(directory)) {
      const path = join(directory, name)
      if (statSync(path).isDirectory()) walk(path)
      else if (/\.tsx?$/.test(name)) files.set(relative(prototype, path), readFileSync(path, 'utf8'))
    }
  }
  for (const module of ['suites', 'tasks']) for (const folder of ['ViewModels', 'Views']) walk(resolve(prototype, 'source/presentation', module, folder))
  return files
}

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

/** What is wrong with the files `preset` names: one no import holds, one missing on disk, a design fixture not named `*-handmade*`, or an empty one. */
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

describe('no recorded task or suite is written into a view or view-model of Suites or Tasks (design 1p)', () => {
  test('the data it looks for: the recordings’ goals, ids and suites', () => {
    const data = recordedData()
    expect(data.goals).toEqual(expect.arrayContaining(['say hello', 'Write the report', 'Find the word', 'Answer when asked', 'Say done, number 56']))
    expect(data.ids).toEqual(expect.arrayContaining(['task-20261021-011456', 'task-20261109-021211', 'task-20261026-060911']))
    expect(data.suites).toEqual(['cloud', 'fast', 'pair', 'shared', 'vast'])
  })

  test('the real views and view-models write none', () => {
    const files = placeFiles()
    expect([...files.keys()]).toEqual(expect.arrayContaining(['source/presentation/tasks/ViewModels/TasksViewModel.ts', 'source/presentation/suites/Views/SuitesScreen.tsx']))
    expect(written(files)).toEqual([])
  })

  test('a planted task row or suite name written into a view or view-model fails, naming the file and the datum', () => {
    const files = placeFiles()
    files.set('source/presentation/tasks/ViewModels/Planted.ts', "export const rows = [{ id: 'task-20261109-021211', goal: 'Write the report' }]\n")
    files.set('source/presentation/suites/Views/Planted.tsx', 'export const Planted = () => <span>vast</span>\n')
    files.set('source/presentation/suites/ViewModels/Commented.ts', '// the suite fast is the default\nexport const steady = 1\n')
    expect(written(files)).toEqual([
      'source/presentation/tasks/ViewModels/Planted.ts writes the recorded "task-20261109-021211"',
      'source/presentation/tasks/ViewModels/Planted.ts writes the recorded "Write the report"',
      'source/presentation/suites/Views/Planted.tsx writes the recorded "vast"',
    ])
  })
})

describe('every file a Suites or Tasks preset reads exists, and traces to a recording or a *-handmade* file (design 1p)', () => {
  for (const preset of suitesPresets) test(preset, () => expect(problems(preset, suitesPlaceNames(), imported(worlds.suites))).toEqual([]))
  for (const [preset, world] of Object.entries(tasksPresets)) test(preset, () => expect(problems(preset, tasksPlaceNames(world), imported(worlds.tasks))).toEqual([]))

  test('a planted preset naming a file nobody imports, or one gone from disk, fails naming the preset and the file', () => {
    const files = imported(worlds.tasks)
    const gone = resolve(prototype, 'fixtures/presentation/tasks/task-gone-handmade.json')
    files.set('task-gone-handmade.json', gone)
    files.set('task-invented.json', resolve(prototype, 'fixtures/presentation/tasks/task-list-handmade.json'))
    expect(problems('tasks:planted', ['task-list.json', 'task-gone-handmade.json', 'task-invented.json'], files)).toEqual([
      'tasks:planted: task-list.json is no fixture its worlds file imports',
      `tasks:planted: task-gone-handmade.json is missing at ${gone}`,
      'tasks:planted: task-invented.json is a design fixture not named *-handmade*',
    ])
  })
})
