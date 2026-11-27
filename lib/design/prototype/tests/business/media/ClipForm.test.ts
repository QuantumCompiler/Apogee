import { describe, expect, test } from 'vitest'
import verdicts from '@recorded/business/media/Fixtures/engine-command-verdicts.txt?raw'
import rain from '../../../fixtures/business/media/clip-rain-window-handmade.json?raw'
import { ClipForm } from '../../../source/business/media/ClipForm'
import { ClipRecord } from '../../../source/business/media/MediaRecords'
import { JSONValue } from '../../../source/data/protocol/JSONValue'

/** The `video generate` lines the CLI took, as engine-command-verdicts.txt recorded them. */
const taken = verdicts
  .split('\n')
  .filter((line) => line.startsWith('taken\tvideo generate'))
  .map((line) => line.slice('taken\t'.length))

const lighthouse = (patch: Partial<ClipForm>): string | undefined => ClipForm.with({ prompt: 'A lighthouse at dusk', ...patch }).arguments('OUT.mp4')?.join(' ')

describe('the clip form: the generate sheet as the command a person types (the Mac’s ClipForm)', () => {
  test('the command test: the default, a model named, --engine torch and --engine mlx are each a line the CLI took', () => {
    const lines = [
      lighthouse({}),
      lighthouse({ model: 'Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3' }),
      lighthouse({ model: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6', engine: 'torch' }),
      lighthouse({ model: 'Wan2.2-TI2V-5B/mlx/5e6f708192a3', engine: 'mlx' }),
      lighthouse({ model: 'Wan2.2-TI2V-5B/mlx/5e6f708192a3' }),
      lighthouse({ model: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6' }),
    ]
    expect(lines).toEqual(taken)
    expect(taken).toHaveLength(6)
  })

  test('every flag set, in the Mac’s order, the prompt last after --', () => {
    const form = ClipForm.with({ prompt: '  -dash first  ', image: '/a.png', model: 'm', seed: 7, duration: 2.5, resolution: '960x544', steps: 30, fps: 12, loras: ['/l.safetensors:0.8'] })
    expect(form.arguments('/o.mp4')).toEqual(['video', 'generate', '-o', '/o.mp4', '--image', '/a.png', '-m', 'm', '--seed', '7', '--duration', '2.5', '--resolution', '960x544', '--steps', '30', '--fps', '12', '--lora', '/l.safetensors:0.8', '--', '-dash first'])
  })

  test('a preset names --draft or --final in place of --steps; --no-lora clears a record’s', () => {
    expect(ClipForm.with({ prompt: 'p', steps: 16, preset: 'draft' }).arguments('o')).toEqual(['video', 'generate', '-o', 'o', '--draft', '--', 'p'])
    expect(ClipForm.with({ start: { case: 'again', record: '/r.json' }, preset: 'final', noLoRAs: true }).arguments('o')).toEqual(['video', 'generate', '--from', '/r.json', '-o', 'o', '--final', '--no-lora'])
  })

  test('a new clip needs a prompt or a seed image; a record run again or a clip continued needs neither', () => {
    expect(ClipForm.with({}).arguments('o')).toBeUndefined()
    expect(ClipForm.with({ image: '/a.png' }).arguments('o')).toEqual(['video', 'generate', '-o', 'o', '--image', '/a.png'])
    expect(ClipForm.with({ start: { case: 'extend', clip: '/c.mp4' } }).arguments('o')).toEqual(['video', 'generate', '--extend', '/c.mp4', '-o', 'o'])
  })

  test('from(): a record run again names its sidecar; continued, its clip', () => {
    const record = ClipRecord.read(JSONValue.parse(rain) ?? null)
    if (!record) throw new Error('no record')
    expect(ClipForm.from(record, false).start).toEqual({ case: 'again', record: '/Users/someone/Movies/Apogee/rain-window.mp4.json' })
    expect(ClipForm.from(record, true).start).toEqual({ case: 'extend', clip: '/Users/someone/Movies/Apogee/rain-window.mp4' })
  })

  test('number(): 5 for 5.0, 2.5 for 2.5', () => {
    expect(ClipForm.number(5)).toBe('5')
    expect(ClipForm.number(2.5)).toBe('2.5')
  })
})
