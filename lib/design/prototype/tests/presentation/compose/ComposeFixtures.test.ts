import { readdirSync, readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { ProjectRecord } from '../../../source/business/media/Projects'
import { JSONValue } from '../../../source/data/protocol/JSONValue'

/** The design's own media fixtures, where this item's hand-made files are. */
const media = resolve(dirname(fileURLToPath(import.meta.url)), '../../../fixtures/business/media')

/** The project document's statuses, the CLI's (machine-mode.md, `video compose`): never the old prototype's `rendered` or `stopped`. */
const statuses = ['rendering', 'done', 'failed', 'cancelled', 'planned']

/** Every key of a JSON value, nested, as `a.b.c`, lists read through their first item: the document's shape. */
function keys(value: JSONValue, at = ''): string[] {
  const array = JSONValue.array(value)
  if (array) return array.length === 0 ? [] : keys(array[0] ?? null, `${at}[]`)
  const object = JSONValue.object(value)
  if (!object) return []
  return Object.keys(object).flatMap((key) => [`${at}${at === '' ? '' : '.'}${key}`, ...keys(object[key] ?? null, `${at}${at === '' ? '' : '.'}${key}`)])
}

/** The keys `video_project_document` always writes (`video_project.cpp`); an edit adds `removed` and its scenes' `from`, `changes` and `moved`. */
const written = ['assembly', 'brief', 'captions', 'created', 'edits', 'error', 'object', 'output', 'parent', 'plan', 'plan.attempts', 'plan.backend', 'platform', 'scenes', 'scenes[].asset', 'scenes[].clip', 'scenes[].index', 'scenes[].narration', 'scenes[].seconds', 'scenes[].seed', 'scenes[].visual', 'scenes[].voice', 'scenes_dir', 'settings', 'settings.assets', 'settings.captions', 'settings.crossfade', 'settings.model', 'settings.music', 'settings.voice', 'settings.voice_model', 'status', 'title', 'version']

/** What is wrong with a hand-made compose file: its name, a document that does not read through Projects.ts, a key the CLI never writes or one it always does missing, a status the CLI never writes. */
function problems(name: string, text: string): string[] {
  const found: string[] = []
  if (!name.includes('-handmade')) found.push(`${name}: not named *-handmade*`)
  const documents = name.endsWith('.jsonl')
    ? text
        .trim()
        .split('\n')
        .flatMap((line) => {
          const event = JSONValue.object(JSONValue.parse(line) ?? null)
          return JSONValue.string(event?.type) === 'document' ? [event?.document ?? null] : []
        })
    : [JSONValue.parse(text) ?? null]
  if (documents.length === 0) found.push(`${name}: no project document`)
  for (const document of documents) {
    const project = ProjectRecord.read(document)
    if (!project) {
      found.push(`${name}: does not read as a video.project`)
      continue
    }
    if (!statuses.includes(project.status)) found.push(`${name}: status ${project.status} is not the CLI's`)
    // The CLI's reader refuses a scene without either (`video_project.cpp`: "a scene has no narration or no visual").
    for (const scene of project.scenes) if (scene.narration === '' || scene.visual === '') found.push(`${name}: scene ${scene.index} has no narration or no visual`)
    const shape = keys(document)
    const missing = written.filter((key) => !shape.includes(key) && !(key.startsWith('scenes[]') && project.scenes.length === 0))
    if (missing.length > 0) found.push(`${name}: lacks ${missing.join(', ')}`)
    const edited = ['removed', 'scenes[].from', 'scenes[].changes', 'scenes[].moved']
    const voice = ['scenes[].voice.file', 'scenes[].voice.seconds', 'scenes[].voice.sentences', 'scenes[].voice.sentences[].text', 'scenes[].voice.sentences[].seconds', 'scenes[].voice.sentences[].offset']
    const clip = ['scenes[].clip.file', 'scenes[].clip.seconds']
    const captions = ['captions.file', 'captions.burned']
    const edits = ['edits[].instruction', 'edits[].redo', 'edits[].from']
    const extra = shape.filter((key) => !written.includes(key) && ![...edited, ...voice, ...clip, ...captions, ...edits].includes(key))
    if (extra.length > 0) found.push(`${name}: the CLI never writes ${extra.join(', ')}`)
  }
  return found
}

/** This item's hand-made compose files: every `compose-*-handmade*` under the design's media fixtures. */
const handmade = readdirSync(media).filter((name) => name.startsWith('compose-'))

describe("the hand-made compose files are the CLI's own shape (design 1l's fixture-shape check)", () => {
  test('every one is named *-handmade*, reads through Projects.ts as a video.project, keeps to the keys video_project_document writes, says only the CLI’s statuses, and gives every scene its narration and visual', () => {
    expect(handmade.length).toBe(34)
    expect(handmade.flatMap((name) => problems(name, readFileSync(join(media, name), 'utf8')))).toEqual([])
  })

  test('the passed-over list is the scan’s own words, file and reason, and named *-handmade*', () => {
    const list = JSON.parse(readFileSync(join(media, 'projects-passed-over-handmade.json'), 'utf8')) as { skipped: Array<{ file: string; reason: string }> }
    expect(list.skipped.map((skip) => Object.keys(skip).sort())).toEqual([
      ['file', 'reason'],
      ['file', 'reason'],
    ])
  })

  test('a planted file with the old prototype’s status, stopped, fails; so does one not named *-handmade*, one with a key the CLI never writes, and one with a scene the CLI’s reader refuses', () => {
    const tide = readFileSync(join(media, 'compose-tide-pools-handmade.json'), 'utf8')
    expect(problems('compose-planted-handmade.json', tide.replace('"status":"cancelled"', '"status":"stopped"'))).toEqual(["compose-planted-handmade.json: status stopped is not the CLI's"])
    expect(problems('compose-planted.json', tide)).toEqual(['compose-planted.json: not named *-handmade*'])
    expect(problems('compose-planted-handmade.json', tide.replace('"status":"cancelled"', '"status":"cancelled","stopNote":"at 21:12"'))).toEqual(['compose-planted-handmade.json: the CLI never writes stopNote'])
    const planted = JSON.parse(tide) as { scenes: Array<Record<string, unknown>> }
    planted.scenes[0] = { ...planted.scenes[0], visual: '' }
    expect(problems('compose-planted-handmade.json', JSON.stringify(planted))).toEqual(['compose-planted-handmade.json: scene 1 has no narration or no visual'])
  })
})
