import { describe, expect, test } from 'vitest'
import checkCaptionsOk from '@recorded/business/reads/Fixtures/check-captions-ok.json?raw'
import checkCaptionsSkipped from '@recorded/business/reads/Fixtures/check-captions-skipped.json?raw'
import editParent from '@recorded/business/media/Fixtures/compose-edit-parent.json?raw'
import editPreview from '@recorded/business/media/Fixtures/compose-edit-preview.json?raw'
import editReplanned from '@recorded/business/media/Fixtures/compose-edit-replanned.json?raw'
import plannedProject from '@recorded/business/media/Fixtures/compose-planned-project.json?raw'
import voicesList from '@recorded/business/reads/Fixtures/compose-voices-list.json?raw'
import voiceList from '@recorded/business/reads/Fixtures/compose-voice-list.json?raw'
import { CaptionsAbility } from '../../../source/business/media/CaptionsAbility'
import { ComposeForm } from '../../../source/business/media/ComposeForm'
import { MediaModel } from '../../../source/business/media/MediaEngine'
import { MediaLibrary } from '../../../source/business/media/MediaRecords'
import { isStill, ProjectLibrary, ProjectRecord } from '../../../source/business/media/Projects'
import type { FolderFile } from '../../../source/business/media/MediaRecords'
import { JSONValue } from '../../../source/data/protocol/JSONValue'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const read = (text: string) => ProjectRecord.read(JSONValue.parse(text) ?? null)
const movies = '/Users/someone/Movies/Apogee'
const folder = (files: readonly FolderFile[]) => ({ files: (path: string) => (path === movies ? files : []) })
const document = (text: string) => ReadDocument.from(text) ?? (() => { throw new Error('not a document') })()

describe("a composition's project: the video.project document beside its video (the Mac's ProjectRecord)", () => {
  test('a dry run’s planned project reads whole: its brief, title, scenes, settings and planner, nothing made', () => {
    const project = read(plannedProject)
    expect(project).toMatchObject({ status: 'planned', brief: 'the tides, twice a day', title: 'Tides', output: `${movies}/tides.mp4`, scenesDir: `${movies}/tides.scenes`, model: 'org--clip/pipeline/aaaaaaaaaaaa', voiceModel: 'hexgrad--Kokoro-82M/voice/cccccccccccc', voice: 'af_heart', captions: true, crossfade: 0.5, music: undefined, assets: undefined, planner: 'planner', attempts: 1, parent: undefined, edits: [], removed: undefined, created: '2026-10-26T04:34:12Z', name: 'tides.mp4' })
    expect(project?.scenes.map((scene) => [scene.index, scene.narration, scene.visual, scene.seconds, scene.seed, scene.voiceFile, scene.clipFile, ProjectRecord.isMade(scene)])).toEqual([
      [1, 'The sea breathes.', 'a shore at dawn', 3, 2003882887, undefined, undefined, false],
      [2, 'Twice a day.', 'the moon over water', 3, 685754606, undefined, undefined, false],
    ])
    expect(project && ProjectRecord.made(project)).toBe(0)
    expect(project && ProjectRecord.hasDiff(project)).toBe(false)
  })

  test('an edit’s dry run reads its parent, its edits and its own comparison: each scene’s from, changes and moved, and removed', () => {
    const project = read(editPreview)
    expect(project).toMatchObject({ output: `${movies}/tides-edit.mp4`, parent: `${movies}/tides.mp4.json`, removed: [], edits: [{ instruction: 'make the second scene the moon', redo: [], from: `${movies}/tides.mp4.json` }] })
    expect(project?.scenes.map((scene) => [scene.from, scene.changes, scene.moved])).toEqual([
      ['1', [], false],
      ['2', ['prompt'], false],
    ])
    expect(project && ProjectRecord.hasDiff(project)).toBe(true)
  })

  test('a scene spoken and rendered reads its files, the narration’s measured seconds and sentences, and the clip’s seconds', () => {
    const value = JSON.parse(plannedProject) as { scenes: Array<Record<string, unknown>>; status: string }
    value.status = 'done'
    value.scenes[0] = { ...value.scenes[0], voice: { file: `${movies}/tides.scenes/s01.wav`, seconds: 1.42, sentences: [{ text: 'The sea breathes.', seconds: 1.42, offset: 0 }] }, clip: { file: `${movies}/tides.scenes/s01.mp4`, seconds: 3 } }
    const scene = read(JSON.stringify(value))?.scenes[0]
    expect(scene).toMatchObject({ voiceFile: `${movies}/tides.scenes/s01.wav`, voiceSeconds: 1.42, sentences: [{ text: 'The sea breathes.', seconds: 1.42, offset: 0 }], clipFile: `${movies}/tides.scenes/s01.mp4`, clipSeconds: 3 })
    expect(scene && ProjectRecord.isMade(scene)).toBe(true)
  })

  test('a scene taken from an asset is made once spoken: its clip is the asset', () => {
    const value = JSON.parse(plannedProject) as { scenes: Array<Record<string, unknown>> }
    value.scenes[0] = { ...value.scenes[0], asset: 'stall-0412.mov', visual: 'Close-up of a market stall at night, food sizzling under string lights.', voice: { file: '/s01.wav', seconds: 1, sentences: [] } }
    const scene = read(JSON.stringify(value))?.scenes[0]
    expect(scene && ProjectRecord.isMade(scene)).toBe(true)
  })

  test('an asset is taken as it is by its extension, as the CLI decides it (compose.cpp’s is_still): a clip is, a still -- png, jpg, jpeg, webp, any case -- is rendered from', () => {
    const value = JSON.parse(plannedProject) as { scenes: Array<Record<string, unknown>> }
    const scene = (asset: string | null) => read(JSON.stringify({ ...value, scenes: [{ ...value.scenes[0], asset, voice: { file: '/s01.wav', seconds: 1, sentences: [] } }] }))!.scenes[0]!
    expect(['moon.mp4', 'stall-0412.MOV', 'beach.png', 'crumb-close.JPG', 'oven.jpeg', 'sky.webp', 'noextension'].map(isStill)).toEqual([false, false, true, true, true, true, false])
    expect([ProjectRecord.takesAsItIs(scene('moon.mp4')), ProjectRecord.takesAsItIs(scene('beach.png')), ProjectRecord.takesAsItIs(scene(null))]).toEqual([true, false, false])
    // A still is a start image: spoken, it is not made until its clip renders.
    expect([ProjectRecord.isMade(scene('moon.mp4')), ProjectRecord.isMade(scene('beach.png'))]).toEqual([true, false])
  })

  test('anything but a video.project with its output is no project', () => {
    expect(read('{"object":"video.generation","output":"/a.mp4"}')).toBeUndefined()
    expect(read('{"object":"video.project","status":"done"}')).toBeUndefined()
  })
})

describe('the projects in the folders (the Mac’s MediaLibrary.projects): newest first, the passed-over said', () => {
  test('each project read beside its video, newest first; one whose project is elsewhere passed over in the scan’s words; a clip’s record not a project', () => {
    const moved = plannedProject.replaceAll(`${movies}/tides`, '/Users/someone/Elsewhere/moved')
    const scan = ProjectLibrary.projects([movies], folder([
      { name: 'tides.mp4.json', text: plannedProject },
      { name: 'tides-edit.mp4.json', text: editPreview },
      { name: 'moved.mp4.json', text: moved },
      { name: 'clip.mp4.json', text: '{"object":"video.generation","output":"/Users/someone/Movies/Apogee/clip.mp4"}' },
    ]))
    expect(scan.records.map((record) => record.name)).toEqual(['tides-edit.mp4', 'tides.mp4'])
    expect(scan.skipped).toEqual([{ file: `${movies}/moved.mp4.json`, reason: 'its file is gone: /Users/someone/Elsewhere/moved.mp4.json' }])
  })

  test('the project beside a video, read again: what the composer saved last', () => {
    const reader = folder([{ name: 'tides.mp4.json', text: plannedProject }])
    expect(ProjectLibrary.beside(`${movies}/tides.mp4`, reader)?.title).toBe('Tides')
    expect(ProjectLibrary.beside(`${movies}/gone.mp4`, reader)).toBeUndefined()
  })
})

describe('an edit’s diff and the render compared with its preview (the Mac’s MediaLibrary.diff, sameChanges)', () => {
  test('the diff from the edit’s own comparison: unchanged, changed with its words, a new scene and a removed one named by the parent', () => {
    const value = JSON.parse(editPreview) as { scenes: Array<Record<string, unknown>>; removed: number[] }
    value.scenes.push({ index: 3, narration: 'A new ending.', visual: 'x', from: 'new', changes: [], moved: false, seed: 1, seconds: 3, asset: null, voice: null, clip: null })
    value.removed = [2]
    const diff = ProjectLibrary.diff(read(JSON.stringify(value))!, read(editParent))
    expect(diff.compared).toBe(false)
    expect(diff.changes).toEqual([
      { index: 1, narration: 'The sea breathes.', kind: { case: 'unchanged' }, moved: false },
      { index: 2, narration: 'Twice a day.', kind: { case: 'changed', words: ['prompt'] }, moved: false },
      { index: 3, narration: 'A new ending.', kind: { case: 'added' }, moved: false },
      { index: 2, narration: 'Twice a day.', kind: { case: 'removed' }, moved: false },
    ])
  })

  test('a document with no comparison of its own is compared scene by scene, and said so', () => {
    const value = JSON.parse(editParent) as { scenes: Array<Record<string, unknown>>; output: string }
    value.output = `${movies}/older-edit.mp4`
    value.scenes[1] = { ...value.scenes[1], visual: 'a full moon rising', seed: 9 }
    const diff = ProjectLibrary.diff(read(JSON.stringify(value))!, read(editParent))
    expect(diff.compared).toBe(true)
    expect(diff.changes.map((change) => change.kind)).toEqual([{ case: 'unchanged' }, { case: 'changed', words: ['prompt', 'seed'] }])
  })

  test('the replanned render differs from its preview; a render the same as its preview but for the seeds does not', () => {
    expect(ProjectLibrary.sameChanges(read(editReplanned)!, read(editPreview)!)).toBe(false)
    const reseeded = editPreview.replace('"seed":761737461', '"seed":5')
    expect(ProjectLibrary.sameChanges(read(reseeded)!, read(editPreview)!)).toBe(true)
  })

  test('planned again, a scene removed: they differ, and the scenes left out are compared too (the Mac’s ProjectsTests.removal)', () => {
    const value = JSON.parse(editPreview) as { scenes: unknown[]; removed: number[] }
    const removed = read(JSON.stringify({ ...value, scenes: value.scenes.slice(0, 1), removed: [2] }))!
    expect(removed.removed).toEqual([2])
    expect(ProjectLibrary.sameChanges(removed, read(editPreview)!)).toBe(false)
    // The same scenes, only the parent's left out differing: still not the same.
    const removedOnly = read(JSON.stringify({ ...value, removed: [2] }))!
    expect(ProjectLibrary.sameChanges(removedOnly, read(editPreview)!)).toBe(false)
  })
})

describe('the compose form as the command a person would type (the Mac’s ComposeForm, with the CLI’s --planner the design draws)', () => {
  test('a new composition: the brief last, after --, every flag the form set and nothing it did not; a dry run plans', () => {
    const form = new ComposeForm()
    expect(form.arguments('/m/a.mp4')).toBeUndefined()
    form.brief = '  the tides, twice a day '
    expect(form.arguments('/m/a.mp4')).toEqual(['video', 'compose', '-o', '/m/a.mp4', '--', 'the tides, twice a day'])
    expect(form.arguments('/m/a.mp4', true)).toEqual(['video', 'compose', '-o', '/m/a.mp4', '--dry-run', '--', 'the tides, twice a day'])
    Object.assign(form, { assets: '~/Pictures/bakery/', music: '~/Music/beds/warm-kitchen.m4a', captions: false, voiceModel: 'Kokoro-82M/voice/5f3a2c91d0e4', voice: 'bm_george', model: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6', planner: 'claude' })
    expect(form.arguments('/m/a.mp4')).toEqual(['video', 'compose', '-o', '/m/a.mp4', '--assets', '~/Pictures/bakery/', '--music', '~/Music/beds/warm-kitchen.m4a', '--no-captions', '--voice-model', 'Kokoro-82M/voice/5f3a2c91d0e4', '--voice', 'bm_george', '-m', 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6', '--planner', 'claude', '--', 'the tides, twice a day'])
  })

  test('an edit: --from, the instruction whole, each scene to re-roll, and only the settings it changes; nothing to run without an instruction or a re-roll', () => {
    const edit = new ComposeForm()
    edit.from = `${movies}/tides.mp4`
    expect(edit.arguments(`${movies}/tides-edit.mp4`)).toBeUndefined()
    edit.instruction = 'make the second scene the moon'
    edit.redo = new Set([2, 1])
    expect(edit.arguments(`${movies}/tides-edit.mp4`, true)).toEqual(['video', 'compose', '--from', `${movies}/tides.mp4`, '--edit', 'make the second scene the moon', '--redo-scene', '1', '--redo-scene', '2', '-o', `${movies}/tides-edit.mp4`, '--dry-run'])
    edit.voice = 'bf_emma'
    edit.captions = false
    expect(edit.arguments(`${movies}/tides-edit.mp4`)).toEqual(['video', 'compose', '--from', `${movies}/tides.mp4`, '--edit', 'make the second scene the moon', '--redo-scene', '1', '--redo-scene', '2', '-o', `${movies}/tides-edit.mp4`, '--no-captions', '--voice', 'bf_emma'])
  })
})

describe('whether this ffmpeg burns the captions, as check says (the Mac’s CaptionsAbility, the CLI’s 52e)', () => {
  test('skipped: kept beside the video, the reason before the consequence, and the remedy; ok: burned', () => {
    const skipped = CaptionsAbility.of(document(checkCaptionsSkipped).value)
    expect(skipped).toEqual({ burns: false, detail: 'no subtitles filter (this ffmpeg was built without libass) -- a composition keeps its captions beside the video as an SRT', remedy: 'brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full' })
    expect(skipped && CaptionsAbility.reason(skipped)).toBe('no subtitles filter (this ffmpeg was built without libass)')
    expect(CaptionsAbility.of(document(checkCaptionsOk).value)?.burns).toBe(true)
    expect(CaptionsAbility.of({ rows: [] })).toBeUndefined()
  })
})

describe('the store’s voice models, as the say form reads them (the Mac’s MediaModel.voiceModels)', () => {
  test('PyTorch first, then MLX; one model, or none', () => {
    expect(MediaModel.voiceModels(document(voicesList)).map((model) => [model.handle, model.engine])).toEqual([
      ['hexgrad--Kokoro-82M/voice/cccccccccccc', 'torch'],
      ['hexgrad--Kokoro-82M/mlx/ffffffffffff', 'mlx'],
    ])
    expect(MediaModel.voiceModels(document(voiceList)).map((model) => model.handle)).toEqual(['hexgrad--Kokoro-82M/voice/cccccccccccc'])
  })
})

test('a free name for a composition is free of the .srt its captions keep beside it, too (the Mac’s freePath(besides:))', () => {
  expect(MediaLibrary.freeName(['tides.srt'], 'tides', 'mp4')).toBe('tides.mp4')
  expect(MediaLibrary.freeName(['tides.srt'], 'tides', 'mp4', ['srt'])).toBe('tides-2.mp4')
})
