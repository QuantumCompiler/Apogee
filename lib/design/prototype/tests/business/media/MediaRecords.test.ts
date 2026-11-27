import { describe, expect, test } from 'vitest'
import clipMlx from '@recorded/business/media/Fixtures/clip-mlx.json?raw'
import clipTorch from '@recorded/business/media/Fixtures/clip-torch.json?raw'
import draft from '../../../fixtures/business/media/clip-lighthouse-dusk-draft-handmade.json?raw'
import final from '../../../fixtures/business/media/clip-lighthouse-dusk-handmade.json?raw'
import extension from '../../../fixtures/business/media/clip-lighthouse-dusk-2-handmade.json?raw'
import harbor from '../../../fixtures/business/media/clip-harbor-first-light-handmade.json?raw'
import rain from '../../../fixtures/business/media/clip-rain-window-handmade.json?raw'
import runStream from '../../../fixtures/business/media/video-generate-run-handmade.jsonl?raw'
import rainStream from '../../../fixtures/business/media/video-generate-rain-handmade.jsonl?raw'
import { ClipRecord, MediaLibrary } from '../../../source/business/media/MediaRecords'
import type { FolderFile } from '../../../source/business/media/MediaRecords'
import { JSONValue } from '../../../source/data/protocol/JSONValue'

const read = (text: string) => ClipRecord.read(JSONValue.parse(text) ?? null)
const movies = '/Users/someone/Movies/Apogee'

/** Every key of a JSON value, nested, as `a.b.c`: the record's shape. */
function keys(value: JSONValue, at = ''): string[] {
  const object = JSONValue.object(value)
  if (!object) return []
  return Object.keys(object).flatMap((key) => {
    const path = at === '' ? key : `${at}.${key}`
    const inner = object[key] ?? null
    // A parent and a LoRA are objects or lists only where a run has them: their own keys are the CLI's video_run_document's.
    return [path, ...(key === 'parent' || key === 'loras' || key === 'versions' ? [] : keys(inner, path))]
  })
}

/** The record a generate stream's closing `document` event carries. */
function streamed(text: string): JSONValue {
  const last = text.trim().split('\n').at(-1) ?? ''
  return JSONValue.object(JSONValue.parse(last))?.document ?? null
}

describe("a clip's record: the video.generation document beside its clip (the Mac's ClipRecord)", () => {
  test('clip-torch.json and clip-mlx.json read whole: the clip, its model, its settings, its device and versions', () => {
    const torch = read(clipTorch)
    expect(torch).toMatchObject({ status: 'done', output: `${movies}/lighthouse-pytorch.mp4`, sidecar: `${movies}/lighthouse-pytorch.mp4.json`, model: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6', modelClass: 'WanPipeline', prompt: 'A lighthouse at dusk', mode: 't2v', seed: 1095329999, seedSource: 'generated' })
    expect(torch).toMatchObject({ width: 256, height: 144, duration: 1, fps: 8, frames: 9, steps: 4, guidance: undefined, codec: 'h264', seconds: 0.179, createdAt: '2026-10-22T09:05:50Z', preset: undefined, image: undefined, loras: [], parentMove: undefined })
    expect(torch?.device).toEqual({ device: 'cpu', dtype: 'float32', platform: 'macos-arm64' })
    expect(torch?.engine).toBe('torch')
    expect(read(clipMlx)?.engine).toBe('mlx')
    expect(read(clipMlx)?.versions).toEqual({ apogee: '0.1.9', diffusers: '', mlx: '0.31.2', mlx_video: '0.0-video-stub', torch: '' })
    expect(torch?.name).toBe('lighthouse-pytorch.mp4')
  })

  test('a record missing its output, or of another object, is none', () => {
    expect(ClipRecord.read({ object: 'voice.speech', output: 'x.wav' })).toBeUndefined()
    expect(ClipRecord.read({ object: 'video.generation' })).toBeUndefined()
  })

  test('the shape check: every hand-made record, and each generate stream’s closing document, parses through the same reader with clip-mlx.json’s keys', () => {
    const shape = keys(JSONValue.parse(clipMlx) ?? null).sort()
    for (const text of [draft, final, extension, harbor, rain]) {
      const value = JSONValue.parse(text) ?? null
      expect(keys(value).sort()).toEqual(shape)
      expect(ClipRecord.read(value)).toBeDefined()
    }
    for (const text of [runStream, rainStream]) {
      expect(keys(streamed(text)).sort()).toEqual(shape)
      expect(ClipRecord.read(streamed(text))?.status).toBe('done')
    }
  })

  test('the lineage: a final run again from its draft, a clip continued, and a LoRA held by its hash', () => {
    expect(read(final)).toMatchObject({ parentMove: 'from', parentClip: `${movies}/lighthouse-dusk-draft.mp4`, parentDocument: `${movies}/lighthouse-dusk-draft.mp4.json`, parentContinued: undefined, preset: 'final', seedSource: 'held' })
    expect(read(extension)).toMatchObject({ parentMove: 'extend', parentClip: `${movies}/lighthouse-dusk.mp4`, parentContinued: `${movies}/lighthouse-dusk.mp4`, mode: 'i2v', image: undefined })
    expect(read(rain)?.loras).toEqual([{ path: '/Users/someone/loras/rain-glass-wan22.safetensors', scale: 0.7, sha256: 'c41d8e02b7a95e3d1f60c84a2e9b7d03f5a1c6e88b2d4f07a39e1c5b6d8f5f7a' }])
    expect(read(rain)?.device.platform).toBe('linux-x86_64')
  })
})

describe('the media library: the records in the folders, as the Mac scans them', () => {
  const file = (name: string, text?: string): FolderFile => (text === undefined ? { name } : { name, text })
  const folder: readonly FolderFile[] = [
    file('rain-window.mp4'),
    file('rain-window.mp4.json', rain),
    file('lighthouse-dusk.mp4'),
    file('lighthouse-dusk.mp4.json', final),
    file('lighthouse-dusk-draft.mp4'),
    file('lighthouse-dusk-draft.mp4.json', draft),
    file('lighthouse-dusk-2.mp4'),
    file('lighthouse-dusk-2.mp4.json', extension),
    // A record whose clip is gone, a sidecar that is not JSON, and another object's record.
    file('harbor-first-light.mp4.json', harbor),
    file('notes.json', 'not json'),
    file('lighthouse-mlx.mp4.json', clipMlx.replace('video.generation', 'voice.speech')),
  ]
  const reader = { files: (path: string) => (path === movies ? folder : []) }

  test('clips(): each record whose clip is there, newest first; what was passed over, and why', () => {
    const { records, skipped } = MediaLibrary.clips([movies], reader)
    expect(records.map((record) => record.name)).toEqual(['lighthouse-dusk-2.mp4', 'lighthouse-dusk.mp4', 'lighthouse-dusk-draft.mp4', 'rain-window.mp4'])
    expect(skipped).toEqual([
      { file: `${movies}/harbor-first-light.mp4.json`, reason: `its file is gone: ${movies}/harbor-first-light.mp4` },
      { file: `${movies}/notes.json`, reason: 'not a JSON document' },
    ])
  })

  test('chain(): each clip a record came from, back to the first', () => {
    const { records } = MediaLibrary.clips([movies], reader)
    const latest = records[0]
    if (!latest) throw new Error('no record')
    expect(MediaLibrary.chain(latest, records).map((record) => record.name)).toEqual(['lighthouse-dusk.mp4', 'lighthouse-dusk-draft.mp4'])
  })

  test('freeName(): a taken name gets a number, never written over -- a clip or its record taking it', () => {
    expect(MediaLibrary.freeName(['lighthouse-dusk.mp4', 'lighthouse-dusk-2.mp4.json'], 'lighthouse-dusk', 'mp4')).toBe('lighthouse-dusk-3.mp4')
    expect(MediaLibrary.freeName([], 'a/b', 'mp4')).toBe('a-b.mp4')
    expect(MediaLibrary.freeName([], ' ', 'mp4')).toBe('untitled.mp4')
  })

  test('installCommand(): the install a refusal names, and the rebuild', () => {
    expect(MediaLibrary.installCommand("apogee video generate: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video")).toEqual(['train', 'setup', '--with', 'video'])
    expect(MediaLibrary.installCommand('… rebuild it on a Python that fits: apogee train setup --rebuild --with voice')).toEqual(['train', 'setup', '--rebuild', '--yes', '--with', 'voice'])
    expect(MediaLibrary.installCommand('apogee video: --engine torch: something else')).toBeUndefined()
    expect(MediaLibrary.rebuilds(['train', 'setup', '--rebuild', '--yes', '--with', 'voice'])).toBe(true)
  })

  test('stem(): a file-name stem from a prompt’s first words', () => {
    expect(MediaLibrary.stem('A lighthouse at dusk')).toBe('a-lighthouse-at-dusk')
    expect(MediaLibrary.stem('One, two; three four five six')).toBe('one-two-three-four-five')
  })
})
