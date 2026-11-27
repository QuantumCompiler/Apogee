import { existsSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
// The rules alone, never the `?raw` imports: a fixture missing on disk fails its presets' tests here, not this file's load.
import { trainingWorldFiles } from '../../../source/presentation/training/Previews/TrainingWorldRules'
import type { TrainingWorld } from '../../../source/presentation/training/Previews/TrainingWorldRules'

const here = dirname(fileURLToPath(import.meta.url))
const prototype = resolve(here, '..', '..', '..')
const worldsFile = resolve(prototype, 'source/presentation/training/Previews/TrainingWorlds.ts')

/** Each training preset and the world its rules answer from (`app/Previews/Presets.ts`'s `trainingPreset`). */
const presets: Readonly<Record<string, TrainingWorld>> = {
  training: 'recorded',
  'training:datasets': 'recorded',
  'training:pipelines': 'pipelines',
  'training:cycle': 'cycle',
  'training:stalelock': 'stalelock',
  'training:unreadable': 'unreadable',
  'training:cyclepass': 'stalelock',
  'training:passrunning': 'passrunning',
  'training:resumeheld': 'resumeheld',
  'training:regimes': 'recorded',
}

/** Every `?raw` import of the worlds' file, by the file name a world names it by, with where it is on disk. */
function imported(): Map<string, string> {
  const files = new Map<string, string>()
  for (const match of readFileSync(worldsFile, 'utf8').matchAll(/from '((?:@recorded|(?:\.\.\/)+fixtures)\/[^'?]+)\?raw'/g)) {
    const path = match[1] ?? ''
    const full = path.startsWith('@recorded/') ? resolve(prototype, '..', '..', 'src', 'darwin', 'tests', path.slice('@recorded/'.length)) : resolve(dirname(worldsFile), path)
    files.set(path.split('/').at(-1) ?? path, full)
  }
  return files
}

/** What is wrong with the files `preset` names: each one no import holds, a file missing on disk, or a design fixture not named `*-handmade*`, each naming the preset. */
function problems(preset: string, names: readonly string[], files: ReadonlyMap<string, string> = imported()): string[] {
  return names.flatMap((name) => {
    const full = files.get(name)
    if (full === undefined) return [`${preset}: ${name} is no fixture TrainingWorlds.ts imports`]
    if (!existsSync(full)) return [`${preset}: ${name} is missing at ${full}`]
    if (full.includes('/lib/design/prototype/fixtures/') && !/-handmade\.[a-z]+$/.test(name)) return [`${preset}: ${name} is a design fixture not named *-handmade*`]
    return []
  })
}

describe('every file a Training preset names exists, and a design fixture says it is hand-made (design 1j)', () => {
  for (const [preset, world] of Object.entries(presets)) {
    test(preset, () => {
      const names = trainingWorldFiles(world)
      expect(names.length, preset).toBeGreaterThan(20)
      expect(problems(preset, names)).toEqual([])
      const files = imported()
      for (const name of names) expect(readFileSync(files.get(name) ?? '').length, `${preset}: ${name} is empty`).toBeGreaterThan(0)
    })
  }

  test('proven on a planted name: a fixture no import holds fails, naming the preset and the file', () => {
    expect(problems('training:cycle', [...trainingWorldFiles('cycle'), 'train-cycle-status-invented.json'])).toEqual(['training:cycle: train-cycle-status-invented.json is no fixture TrainingWorlds.ts imports'])
  })

  test('proven on a planted path and a planted name: an import whose file is not on disk, and a design fixture not named hand-made, each fail naming the preset', () => {
    const files = imported()
    const gone = resolve(prototype, 'fixtures/business/reads/train-status-gone-handmade.json')
    const invented = resolve(prototype, 'fixtures/business/reads/train-promote-gate-failed-handmade.err')
    files.set('train-status-gone-handmade.json', gone)
    files.set('train-promote-invented.err', invented)
    expect(problems('training', ['train-status.json', 'train-status-gone-handmade.json', 'train-promote-invented.err'], files)).toEqual([
      `training: train-status-gone-handmade.json is missing at ${gone}`,
      'training: train-promote-invented.err is a design fixture not named *-handmade*',
    ])
  })
})
