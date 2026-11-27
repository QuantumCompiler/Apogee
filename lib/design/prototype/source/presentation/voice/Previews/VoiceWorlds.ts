import speechMlx from '@recorded/business/media/Fixtures/speech-mlx.json?raw'
import speechTorch from '@recorded/business/media/Fixtures/speech-torch.json?raw'
import modelsStoredInfoVoice from '@recorded/business/reads/Fixtures/models-stored-info-voice.json?raw'
import sayDone from '../../../../fixtures/business/media/voice-say-done-handmade.jsonl?raw'
import modelsListVoice from '../../../../fixtures/business/reads/models-list-voice-handmade.json?raw'
import type { Rule, Rules } from '../../../infrastructure/process/StandInChild'
import type { FolderFile, Folders } from '../../../infrastructure/process/StandInFolders'
import { musicFolder } from '../ViewModels/VoiceViewModel'

export { musicFolder }

/** The store's voice model: Kokoro as hexgrad publishes it, the one `models-stored-list.json` lists. */
export const voiceModel = 'Kokoro-82M/voice/5f3a2c91d0e4'

/** A speech's WAV in `~/Music/Apogee`. */
export const speechPath = (file: string) => `${musicFolder}/${file}`

/** The speeches, by their WAV: the two the CLI recorded (`speech-torch.json`, `speech-mlx.json`), and the one the say stream makes. */
export const speeches = {
  torch: speechPath('hello-pytorch.wav'),
  mlx: speechPath('hello-mlx.wav'),
  said: speechPath('apogee-runs-every.wav'),
} as const

/** The text the speak card holds: the old prototype's `vf.text`, the line the hand-made say stream speaks. */
export const sampleText = 'Apogee runs every model on this Mac unless you pick a cloud one. When it does use the cloud, it tells you, every time.'

/** Where the `voice` preset's player stands: paused 0.06 s into `speech-torch.json`, on its second sentence (0.05 to 0.1 s), as the old frame pauses 3.4 s into its second. */
export const secondSentence = 0.06

/** `apogee voice say -o <~/Music/Apogee/apogee-runs-every.wav> -- <the sample text>`, the 52b stream asked for: the run the speak card makes of its sample, with the store's one voice model. */
export const sayArguments = ['voice', 'say', '-o', speeches.said, '--output-format', 'stream-json', '--', sampleText]

/** The record the say stream's closing document carries, as the CLI writes it beside the WAV (`result.document.dump(2)`). */
export const saidRecord = (() => {
  const last = sayDone.trim().split('\n').at(-1) ?? '{}'
  return `${JSON.stringify((JSON.parse(last) as { document: unknown }).document, null, 2)}\n`
})()

/** The records beside their WAVs, as the CLI wrote them: the two recorded speeches. */
const records: ReadonlyArray<readonly [string, string]> = [
  ['hello-pytorch.wav', speechTorch],
  ['hello-mlx.wav', speechMlx],
]

/** `~/Music/Apogee`'s files as the stand-in answers them: each WAV, and its record beside it. */
export function musicFiles(): FolderFile[] {
  return records.flatMap(([file, record]): FolderFile[] => [{ name: file }, { name: `${file}.json`, text: record }])
}

/** `folders` with `~/Music/Apogee`'s speeches beside them, unless they name that folder already: every preset's, so Voice opens whole anywhere. */
export function voiceFolders(folders: Folders = {}): Folders {
  return folders[musicFolder] !== undefined ? folders : { ...folders, [musicFolder]: musicFiles() }
}

/** The same folder with one WAV gone, its record left: what the library passes over, and says why. */
export function voiceFoldersWithoutSpeech(file: string): Folders {
  return { [musicFolder]: musicFiles().filter((entry) => entry.name !== file) }
}

/** The same folder with a record that lacks its segments beside the recorded two: passed over and said, never a throw. */
export function voiceFoldersWithUntimedRecord(): Folders {
  const value = JSON.parse(speechTorch) as Record<string, unknown>
  delete value.segments
  value.output = speechPath('no-timings.wav')
  value.sidecar = speechPath('no-timings.wav.json')
  return { [musicFolder]: [...musicFiles(), { name: 'no-timings.wav' }, { name: 'no-timings.wav.json', text: JSON.stringify(value) }] }
}

/**
 * The store's `models list` for the Voice world, hand-made in the CLI's
 * shape (`models-list-voice-handmade.json`), as 1h's, 1i's and 1k's worlds'
 * lists are: `models-stored-list.json`'s media sets -- Kokoro, its one voice
 * model, and the Wan 2.2 pipeline, whose `models info` every preset answers
 * -- beside the sessions' configured backends (`models-list-kinds-handmade.json`'s
 * rows), so the sidebar's glyphs and the session behind keep their facts, in
 * the CLI's order (the unconfigured sets, then the backends by name), its
 * `folded` kept. The recording's three chat-model sets are left out, since
 * the Models place reads every set the list names and no `models info` of
 * its Qwen3-VL-8B was recorded; its sandbox backend (`mock-chat`) gives way
 * to the presets' configuration.
 */
export const voiceModelsList = modelsListVoice

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })

/**
 * The Voice place's answers on every preset (design 1m), so it opens whole
 * from any screen: the sample spoken (`voice-say-done-handmade.jsonl`,
 * answered for its exact arguments), its WAV and its record written into
 * `~/Music/Apogee` once its document has played (the stand-in's `writes`,
 * 1l), so the place reads the new speech back from the folder, as the Mac
 * does. The voice model's `models info` is Settings' on every preset.
 */
export function voicePlaceRules(): Required<Pick<Rules, 'jobs'>> {
  const lines = sayDone.trim().split('\n').length
  return {
    jobs: [
      {
        arguments: sayArguments,
        exact: true,
        answer: {
          stdout: sayDone,
          writes: [
            { after: lines, folder: musicFolder, file: { name: 'apogee-runs-every.wav' } },
            { after: lines, folder: musicFolder, file: { name: 'apogee-runs-every.wav.json', text: saidRecord } },
          ],
        },
      },
    ],
  }
}

/** The Voice preset's world: every preset's answers, the store's `models list` with its one voice model, and that model's `models info`, its three voices (`models-stored-info-voice.json`). */
export function voiceWorld(): Required<Pick<Rules, 'reads' | 'jobs'>> {
  return { ...voicePlaceRules(), reads: [read(['models', 'list'], voiceModelsList), read(['models', 'info', voiceModel], modelsStoredInfoVoice)] }
}

/** `rules` with the Voice world's beside them, the world's later, so its `models list` answers the session's read too. */
export function withVoiceWorld(rules: Rules): Rules {
  const voice = voiceWorld()
  return { ...rules, reads: [...(rules.reads ?? []), ...voice.reads], jobs: [...(rules.jobs ?? []), ...voice.jobs] }
}
