import { describe, expect, test } from 'vitest'
import verdicts from '@recorded/business/media/Fixtures/engine-command-verdicts.txt?raw'
import { SayForm } from '../../../source/business/media/SayForm'

/** The `voice say` lines the CLI took, as engine-command-verdicts.txt recorded them (the last one's note on the store dropped). */
const taken = verdicts
  .split('\n')
  .filter((line) => line.startsWith('taken\tvoice say'))
  .map((line) => line.slice('taken\t'.length).replace(' (one voice model in the store)', ''))

const hello = (patch: Partial<SayForm>): string | undefined => SayForm.with({ text: 'Hello there. How are you?', ...patch }).arguments('OUT.wav')?.join(' ')

describe('the say form: the speak card as the command a person types (the Mac’s plannedArguments)', () => {
  test('the command test: a model named with and without its engine, a voice and a speed, and the store’s one model unnamed, are each a line the CLI took', () => {
    const lines = [
      hello({ model: 'Kokoro-82M/voice/5f3a2c91d0e4', engine: 'torch' }),
      hello({ model: 'Kokoro-82M/voice/5f3a2c91d0e4' }),
      hello({ model: 'Kokoro-82M/mlx/6f708192a3b4' }),
      hello({ model: 'Kokoro-82M/mlx/6f708192a3b4', engine: 'mlx' }),
      hello({ model: 'Kokoro-82M/voice/5f3a2c91d0e4', engine: 'torch', voice: 'bf_emma', speed: 1.2 }),
      hello({ model: 'Kokoro-82M/mlx/6f708192a3b4', engine: 'mlx', voice: 'bf_emma', speed: 1.2 }),
      hello({}),
    ]
    expect(lines).toEqual(taken)
    expect(taken).toHaveLength(7)
  })

  test('the CLI’s default voice and speed are never named; nothing to say is no command; the text is the last word, after --, trimmed', () => {
    expect(SayForm.with({ text: ' Hi. ', voice: SayForm.defaultVoice, speed: 1 }).arguments('/a.wav')).toEqual(['voice', 'say', '-o', '/a.wav', '--', 'Hi.'])
    expect(SayForm.with({ text: '-x', voice: 'am_adam', speed: 0.85 }).arguments('/a.wav')).toEqual(['voice', 'say', '-o', '/a.wav', '--voice', 'am_adam', '--speed', '0.85', '--', '-x'])
    expect(SayForm.with({ text: '  ' }).arguments('/a.wav')).toBeUndefined()
    // --engine names the engine of the model -m names (the Mac's `chosenEngine`): with no model named, the directory decides and none is passed.
    expect(SayForm.with({ text: 'Hi.', engine: 'torch' }).arguments('/a.wav')).toEqual(['voice', 'say', '-o', '/a.wav', '--', 'Hi.'])
  })

  test('the CLI’s cap and default voice, as voice_say.h and say_text name them', () => {
    expect(SayForm.maxCharacters).toBe(4000)
    expect(SayForm.defaultVoice).toBe('af_heart')
  })
})
