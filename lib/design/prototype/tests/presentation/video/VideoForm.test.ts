import { describe, expect, test } from 'vitest'
import draft from '../../../fixtures/business/media/clip-lighthouse-dusk-draft-handmade.json?raw'
import rain from '../../../fixtures/business/media/clip-rain-window-handmade.json?raw'
import { ClipRecord } from '../../../source/business/media/MediaRecords'
import type { MediaModel } from '../../../source/business/media/MediaEngine'
import { JSONValue } from '../../../source/data/protocol/JSONValue'
import { VideoForm, framesOf, stemOf, tilde } from '../../../source/presentation/video/ViewModels/VideoForm'
import type { FormContext } from '../../../source/presentation/video/ViewModels/VideoForm'

const record = (text: string) => {
  const read = ClipRecord.read(JSONValue.parse(text) ?? null)
  if (!read) throw new Error('no record')
  return read
}
const models: MediaModel[] = [
  { handle: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6', engine: 'torch', bytes: 462 },
  { handle: 'Wan2.2-TI2V-5B/mlx/5e6f708192a3', engine: 'mlx', bytes: undefined },
]
const context: FormContext = { models, defaultModel: 'Wan2.2-TI2V-5B/mlx/5e6f708192a3', taken: ['lighthouse-dusk.mp4', 'lighthouse-dusk.mp4.json', 'lighthouse-dusk-draft.mp4', 'rain-window.mp4'], platform: 'macos-arm64' }

/** A command's options and values, `~/` paths made whole: what the drawn command and the run's argument list must share, whatever their order. */
function flags(words: readonly string[]): string[] {
  const out: string[] = []
  for (let at = 0; at < words.length; at += 1) {
    const word = words[at] ?? ''
    if (word.startsWith('-') && word !== '--') {
      const next = words[at + 1]
      const value = next !== undefined && !next.startsWith('-') ? next : ''
      if (value !== '') at += 1
      out.push(`${word === '--model' ? '-m' : word} ${value.replace(/^~\//, '/Users/someone/')}`.trim())
    }
  }
  return out.sort()
}

describe('the generate form: the old prototype’s `cf`, its words and its command (design 1k)', () => {
  test('the drawn command, in the prototype’s order, names exactly the flags of the run’s argument list in the CLI’s verdict order', () => {
    const forms: Array<[VideoForm, ClipRecord | undefined]> = [
      [VideoForm.fresh(context.defaultModel ?? '', 'A paper boat'), undefined],
      [{ ...VideoForm.fresh(models[0]?.handle ?? '', 'A paper boat'), dur: '3', fps: '12', size: '960x544', seedMode: 'given', seed: '9', steps: '30', preset: 'custom' }, undefined],
      [{ ...VideoForm.from(record(draft), 'from'), preset: 'final', steps: '50' }, record(draft)],
      [{ ...VideoForm.from(record(rain), 'from'), loras: [], loraEdited: true }, record(rain)],
      [VideoForm.from(record(draft), 'extend'), record(draft)],
    ]
    for (const [form, from] of forms) {
      const output = VideoForm.output(form, from, context.taken)
      const drawn = VideoForm.command(form, from, context).replace(/^apogee video generate /, '').replace(/"[^"]*"/, 'PROMPT').split(' ')
      const run = VideoForm.clipForm(form, from, context).arguments(`/Users/someone/Movies/Apogee/${output}`) ?? []
      expect(flags(drawn), VideoForm.command(form, from, context)).toEqual(flags(run.slice(2, run.includes('--') ? run.indexOf('--') : run.length)))
    }
  })

  test('the file a run writes: the record’s stem, -draft for a draft, never a name taken; a new clip’s from its prompt', () => {
    expect(VideoForm.clipName({ ...VideoForm.from(record(draft), 'from'), preset: 'final', steps: '50' }, record(draft), context.taken)).toBe('lighthouse-dusk-2.mp4')
    expect(VideoForm.clipName(VideoForm.fresh('', 'A paper boat drifting down a rain-filled gutter'), undefined, [])).toBe('paper-boat-drifting-draft.mp4')
    expect(stemOf('The, of; and')).toBe('untitled')
    expect(framesOf(5, 24)).toBe(121)
    expect(framesOf(4, 24)).toBe(97)
    expect(tilde('/Users/someone/Pictures/x.jpg')).toBe('~/Pictures/x.jpg')
  })

  test('the pipeline’s check: only where the store lists video models; a store that lists none leaves it to the CLI', () => {
    const gone = { ...VideoForm.fresh('Nope/pipeline/0', 'x') }
    expect(VideoForm.errors(gone, context)).toContain('That pipeline isn’t in the library any more. Pick another, or pull one in Models.')
    expect(VideoForm.errors(gone, { ...context, models: [] })).toEqual([])
  })

  test('LoRAs removed from a record run again: --no-lora, and the told line says no LoRAs', () => {
    const cleared = { ...VideoForm.from(record(rain), 'from'), loras: [], loraEdited: true }
    expect(VideoForm.clipForm(cleared, record(rain), context).arguments('/o.mp4')).toEqual(['video', 'generate', '--from', '/Users/someone/Movies/Apogee/rain-window.mp4.json', '-o', '/o.mp4', '--no-lora'])
    expect(VideoForm.told(cleared, record(rain), context).at(-1)).toBe('changed: no LoRAs')
  })
})
