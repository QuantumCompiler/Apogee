import installDone from '@recorded/business/media/Fixtures/install-video-done.txt?raw'
import installFailed from '@recorded/business/media/Fixtures/install-video-failed.txt?raw'
import installFailedErr from '@recorded/business/media/Fixtures/install-video-failed.err?raw'
import mediaDefaultVideo from '@recorded/business/reads/Fixtures/media-default-video.txt?raw'
import clipDraft from '../../../../fixtures/business/media/clip-lighthouse-dusk-draft-handmade.json?raw'
import clipFinal from '../../../../fixtures/business/media/clip-lighthouse-dusk-handmade.json?raw'
import clipExtension from '../../../../fixtures/business/media/clip-lighthouse-dusk-2-handmade.json?raw'
import clipHarbor from '../../../../fixtures/business/media/clip-harbor-first-light-handmade.json?raw'
import clipRain from '../../../../fixtures/business/media/clip-rain-window-handmade.json?raw'
import refusedPeft from '../../../../fixtures/business/media/generate-refused-peft-handmade.jsonl?raw'
import refusedPeftErr from '../../../../fixtures/business/media/generate-refused-peft-handmade.err?raw'
import generateRain from '../../../../fixtures/business/media/video-generate-rain-handmade.jsonl?raw'
import generateRun from '../../../../fixtures/business/media/video-generate-run-handmade.jsonl?raw'
import modelsInfoPipeline from '../../../../fixtures/business/reads/models-info-wan-pipeline-handmade.json?raw'
import modelsListVideo from '../../../../fixtures/business/reads/models-list-video-handmade.json?raw'
import type { FolderFile, Folders } from '../../../infrastructure/process/StandInFolders'
import type { Answer, Rule, Rules } from '../../../infrastructure/process/StandInChild'
import { moviesFolder } from '../ViewModels/VideoForm'

/** The store's Wan 2.2 pipeline: what the hand-made clips ran on (`clip-torch.json`'s model). */
export const pipelineHandle = 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6'

/** The clips the Video place lists, by their file: the old prototype's five, each a hand-made `video.generation` record in the CLI's shape. */
export const clips = {
  extension: 'lighthouse-dusk-2.mp4',
  final: 'lighthouse-dusk.mp4',
  draft: 'lighthouse-dusk-draft.mp4',
  harbor: 'harbor-first-light.mp4',
  rain: 'rain-window.mp4',
} as const

/** A clip's absolute path in `~/Movies/Apogee`. */
export const clipPath = (file: string) => `${moviesFolder}/${file}`

/** The prompt the `video` presets' new-clip form holds: the old frame's `cf.prompt`. */
export const paperBoat = 'A paper boat drifting down a rain-filled gutter, low angle, shallow depth of field'

/** The records beside their clips, in the folder, as the CLI wrote them: the old prototype's five. */
const records: ReadonlyArray<readonly [string, string]> = [
  [clips.extension, clipExtension],
  [clips.final, clipFinal],
  [clips.draft, clipDraft],
  [clips.harbor, clipHarbor],
  [clips.rain, clipRain],
]

/** `~/Movies/Apogee` as the stand-in answers it: each clip, and its record beside it. */
export function videoFolders(): Folders {
  return { [moviesFolder]: records.flatMap(([file, record]): FolderFile[] => [{ name: file }, { name: `${file}.json`, text: record }]) }
}

/** The same folder with one clip gone, its record left: what the library passes over, and says why. */
export function videoFoldersWithoutClip(file: string): Folders {
  return { [moviesFolder]: (videoFolders()[moviesFolder] ?? []).filter((entry) => entry.name !== file) }
}

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })
/** With the 52b stream asked for, as the place asks for it: the arguments the run is answered by, whole (1h's `exact`). */
const streamed = (args: readonly string[]) => [...args, '--output-format', 'stream-json']

/** `apogee video generate --from <the draft's record> -o lighthouse-dusk-3.mp4 --final`: the `video:run` preset's run, its draft at 50 steps. */
export const finalFromDraft = ['video', 'generate', '--from', `${clipPath(clips.draft)}.json`, '-o', clipPath('lighthouse-dusk-3.mp4'), '--final']
/** `apogee video generate --from <rain's record> -o rain-window-2.mp4`: rain run again, its LoRA held -- refused for peft until the install goes in. */
export const rainAgain = ['video', 'generate', '--from', `${clipPath(clips.rain)}.json`, '-o', clipPath('rain-window-2.mp4')]
/** `apogee train setup --with video`: what the refusal names. */
export const installVideo = ['train', 'setup', '--with', 'video']

/** The LoRA refusal (`video_generate.cpp:696`), hand-made in the M29 stream's shape: its error, stderr's line, exit 1. */
const refusal: Answer = { stdout: refusedPeft, stderr: refusedPeftErr, exit: 1 }
/** How long an install stands before it ends, past its lines: the old prototype's 11 ticks in all. */
const installTicks = 11

/**
 * The Video place's answers on every preset (design 1k), so it opens whole
 * from any screen: the pipeline's `models info` (the Models place reads every
 * set the list names), the default (`config get models.default_video`, as
 * Settings reads it), and the runs: the draft made final, rain run again
 * (refused for peft until the install has gone in -- however often Generate
 * is pressed, and whoever ran the install -- then made), and the install,
 * failing once as the old prototype's does, then going in
 * (`install-video-failed.*`, then `install-video-done.txt`, resting on its
 * `installing …` line until its end, as the old card does).
 */
export function videoPlaceRules(): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  return {
    reads: [read(['models', 'info', pipelineHandle], modelsInfoPipeline)],
    commands: [read(['config', 'get', 'models.default_video'], mediaDefaultVideo)],
    jobs: [
      { arguments: streamed(finalFromDraft), exact: true, answer: { stdout: generateRun } },
      { arguments: streamed(rainAgain), exact: true, until: { ran: installVideo, answer: refusal }, answer: { stdout: generateRain } },
      {
        arguments: installVideo,
        exact: true,
        answer: [
          { stdout: installFailed, stderr: installFailedErr, exit: 1, exitAfter: installTicks - installFailed.trim().split('\n').length - 1 },
          { stdout: installDone, holdAt: 2, exitAfter: installTicks - installDone.trim().split('\n').length - 1 },
        ],
      },
    ],
  }
}

/** The Video presets' world: every preset's answers, and the store's `models list` with the Wan 2.2 pipeline beside the media sets Settings lists (`models-list-video-handmade.json`). */
export function videoWorld(): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  const place = videoPlaceRules()
  return { ...place, reads: [...place.reads, read(['models', 'list'], modelsListVideo)] }
}

/** `rules` with the Video world's beside them, the world's later, so its `models list` answers the session's read too. */
export function withVideoWorld(rules: Rules): Rules {
  const video = videoWorld()
  return { ...rules, reads: [...(rules.reads ?? []), ...video.reads], commands: [...(rules.commands ?? []), ...video.commands], jobs: [...(rules.jobs ?? []), ...video.jobs] }
}
