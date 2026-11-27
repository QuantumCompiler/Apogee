import { describe, expect, test } from 'vitest'
import speechMlx from '@recorded/business/media/Fixtures/speech-mlx.json?raw'
import speechTorch from '@recorded/business/media/Fixtures/speech-torch.json?raw'
import sayDone from '../../../fixtures/business/media/voice-say-done-handmade.jsonl?raw'
import { MediaLibrary, SpeechRecord } from '../../../source/business/media/MediaRecords'
import type { FolderFile } from '../../../source/business/media/MediaRecords'
import { JSONValue } from '../../../source/data/protocol/JSONValue'

const read = (text: string) => SpeechRecord.read(JSONValue.parse(text) ?? null)
const music = '/Users/someone/Music/Apogee'

/** Every key of a JSON value, nested, as `a.b.c`: the record's shape (a segment's and a sentence's keys from their first). */
function keys(value: JSONValue, at = ''): string[] {
  const object = JSONValue.object(value)
  const list = JSONValue.array(value)
  if (list) return list.length === 0 ? [] : keys(list[0] ?? null, `${at}[]`)
  if (!object) return []
  return Object.keys(object).flatMap((key) => {
    const path = at === '' ? key : `${at}.${key}`
    return [path, ...keys(object[key] ?? null, path)]
  })
}

/** The record the hand-made stream's closing `document` event carries. */
function streamed(text: string): JSONValue {
  const last = text.trim().split('\n').at(-1) ?? ''
  return JSONValue.object(JSONValue.parse(last))?.document ?? null
}

describe("a speech's record: the voice.speech document beside its WAV (the Mac's SpeechRecord)", () => {
  test('speech-torch.json reads whole: the WAV, its model, voice and language, speed, text, its sentences with the record’s own timings, the file’s seconds, rate, device, engine and versions', () => {
    const torch = read(speechTorch)
    expect(torch).toMatchObject({
      status: 'done',
      output: `${music}/hello-pytorch.wav`,
      sidecar: `${music}/hello-pytorch.wav.json`,
      model: 'Kokoro-82M/voice/5f3a2c91d0e4',
      voice: 'af_heart',
      language: 'American English',
      speed: 1,
      text: 'Hello there. How are you?',
      segments: 1,
      seconds: 0.1,
      sampleRate: 24000,
      runSeconds: 0.091,
      createdAt: '2026-10-22T09:05:50Z',
      engine: 'torch',
      error: undefined,
      name: 'hello-pytorch.wav',
    })
    expect(torch?.sentences).toEqual([
      { text: 'Hello there.', offset: 0, seconds: 0.05 },
      { text: 'How are you?', offset: 0.05, seconds: 0.05 },
    ])
    expect(torch?.device).toEqual({ device: 'cpu', platform: 'macos-arm64' })
    expect(torch?.versions).toEqual({ apogee: '0.1.9', kokoro: '0.0-stub', torch: '0.0-stub' })
    const mlx = read(speechMlx)
    expect(mlx).toMatchObject({ model: 'Kokoro-82M/mlx/6f708192a3b4', engine: 'mlx', runSeconds: 0.108, name: 'hello-mlx.wav' })
    expect(mlx?.device.device).toBe('gpu')
  })

  test('a segment’s sentences are laid onto the file’s own offsets: a segment’s offset, then the sentence’s within it (the Mac’s rule)', () => {
    const value = JSONValue.parse(speechTorch) as Record<string, JSONValue>
    const two = { ...value, segments: [{ offset: 0, seconds: 0.1, text: 'a', sentences: [{ offset: 0, seconds: 0.1, text: 'One.' }] }, { offset: 0.1, seconds: 0.2, text: 'b', sentences: [{ offset: 0.05, seconds: 0.15, text: 'Two.' }] }] }
    expect(SpeechRecord.read(two)?.sentences).toEqual([
      { text: 'One.', offset: 0, seconds: 0.1 },
      { text: 'Two.', offset: 0.15000000000000002, seconds: 0.15 },
    ])
    expect(SpeechRecord.read(two)?.segments).toBe(2)
  })

  test('the sentence at a time, by the record’s own timings: 0.06 s in speech-torch.json is the second (index 1), the start the first, past the end none', () => {
    const torch = read(speechTorch)
    if (!torch) throw new Error('no record')
    expect(SpeechRecord.sentence(torch, 0.06)).toBe(1)
    expect(SpeechRecord.sentence(torch, 0)).toBe(0)
    expect(SpeechRecord.sentence(torch, 0.03)).toBe(0)
    expect(SpeechRecord.sentence(torch, 0.2)).toBeUndefined()
    expect(SpeechRecord.duration(torch)).toBe(0.1)
  })

  test('a record missing its output, or of another object, is none; one missing its seconds lasts to its last sentence’s end', () => {
    expect(SpeechRecord.read({ object: 'video.generation', output: 'x.mp4' })).toBeUndefined()
    expect(SpeechRecord.read({ object: 'voice.speech' })).toBeUndefined()
    const value = JSONValue.parse(speechTorch) as Record<string, JSONValue>
    const { seconds: _seconds, ...unmeasured } = value
    const record = SpeechRecord.read(unmeasured)
    if (!record) throw new Error('no record')
    expect(record.seconds).toBeUndefined()
    expect(SpeechRecord.duration(record)).toBe(0.1)
  })

  test('the shape check: the hand-made say stream’s closing document has speech-torch.json’s keys, and reads through the same reader', () => {
    expect(keys(streamed(sayDone)).sort()).toEqual(keys(JSONValue.parse(speechTorch) ?? null).sort())
    expect(SpeechRecord.read(streamed(sayDone))).toMatchObject({ status: 'done', output: `${music}/apogee-runs-every.wav`, engine: 'torch', segments: 1 })
  })
})

describe('the speeches in the folders, as the Mac scans them (MediaLibrary.speeches)', () => {
  const file = (name: string, text?: string): FolderFile => (text === undefined ? { name } : { name, text })
  const withoutSegments = (() => {
    const value = JSONValue.parse(speechTorch) as Record<string, JSONValue>
    const { segments: _segments, ...rest } = value
    return JSON.stringify({ ...rest, output: `${music}/no-timings.wav` })
  })()
  const folder: readonly FolderFile[] = [
    file('hello-pytorch.wav'),
    file('hello-pytorch.wav.json', speechTorch),
    file('hello-mlx.wav'),
    file('hello-mlx.wav.json', speechMlx),
    // A record without its segments, one whose WAV is gone, and a clip's record.
    file('no-timings.wav'),
    file('no-timings.wav.json', withoutSegments),
    file('gone.wav.json', speechTorch.replace('hello-pytorch.wav', 'gone.wav')),
    file('a-clip.mp4.json', '{"object":"video.generation","output":"/x.mp4"}'),
  ]
  const reader = { files: (path: string) => (path === music ? folder : []) }

  test('each record whose WAV is there, newest first (a tie in the scan’s order); a record without its segments passed over and said, never a throw', () => {
    const { records, skipped } = MediaLibrary.speeches([music], reader)
    expect(records.map((record) => record.name)).toEqual(['hello-mlx.wav', 'hello-pytorch.wav'])
    expect(skipped).toEqual([
      { file: `${music}/gone.wav.json`, reason: `its file is gone: ${music}/gone.wav` },
      { file: `${music}/no-timings.wav.json`, reason: 'a voice.speech record without its segments' },
    ])
  })
})
