import checkCaptionsSkipped from '@recorded/business/reads/Fixtures/check-captions-skipped.json?raw'
import composeVoicesList from '@recorded/business/reads/Fixtures/compose-voices-list.json?raw'
import editPreview from '@recorded/business/media/Fixtures/compose-edit-preview.json?raw'
import editPreviewErr from '@recorded/business/media/Fixtures/compose-edit-preview.err?raw'
import editRedo from '@recorded/business/media/Fixtures/compose-edit-redo.json?raw'
import editRedoErr from '@recorded/business/media/Fixtures/compose-edit-redo.err?raw'
import editReroll from '@recorded/business/media/Fixtures/compose-edit-reroll-preview.json?raw'
import mediaDefaultVideo from '@recorded/business/reads/Fixtures/media-default-video.txt?raw'
import modelsStoredInfoVoice from '@recorded/business/reads/Fixtures/models-stored-info-voice.json?raw'
import planned from '@recorded/business/media/Fixtures/compose-planned.jsonl?raw'
import storeInfoKindVoice from '@recorded/business/reads/Fixtures/store-info-kind-voice.json?raw'
import heatPump from '../../../../fixtures/business/media/compose-heat-pump-handmade.json?raw'
import nightMarket from '../../../../fixtures/business/media/compose-night-market-handmade.json?raw'
import nightMarket2 from '../../../../fixtures/business/media/compose-night-market-2-handmade.json?raw'
import tidePools from '../../../../fixtures/business/media/compose-tide-pools-handmade.json?raw'
import tidesDone from '../../../../fixtures/business/media/compose-tides-handmade.json?raw'
import passedOverList from '../../../../fixtures/business/media/projects-passed-over-handmade.json?raw'
import modelsListVideo from '../../../../fixtures/business/reads/models-list-video-handmade.json?raw'
import renderNew from '../../../../fixtures/business/media/compose-render-new-handmade.jsonl?raw'
import renderNewSaved1 from '../../../../fixtures/business/media/compose-render-new-saved-1-handmade.json?raw'
import renderNewSaved2 from '../../../../fixtures/business/media/compose-render-new-saved-2-handmade.json?raw'
import renderNewSaved3 from '../../../../fixtures/business/media/compose-render-new-saved-3-handmade.json?raw'
import renderNewSaved4 from '../../../../fixtures/business/media/compose-render-new-saved-4-handmade.json?raw'
import renderNewSaved5 from '../../../../fixtures/business/media/compose-render-new-saved-5-handmade.json?raw'
import renderNewSaved6 from '../../../../fixtures/business/media/compose-render-new-saved-6-handmade.json?raw'
import renderNewStopped1 from '../../../../fixtures/business/media/compose-render-new-stopped-1-handmade.json?raw'
import renderNewStopped2 from '../../../../fixtures/business/media/compose-render-new-stopped-2-handmade.json?raw'
import renderNewStopped3 from '../../../../fixtures/business/media/compose-render-new-stopped-3-handmade.json?raw'
import renderNewStopped4 from '../../../../fixtures/business/media/compose-render-new-stopped-4-handmade.json?raw'
import renderNewStopped5 from '../../../../fixtures/business/media/compose-render-new-stopped-5-handmade.json?raw'
import resumeTide from '../../../../fixtures/business/media/compose-resume-tide-pools-handmade.jsonl?raw'
import resumeTideSaved1 from '../../../../fixtures/business/media/compose-resume-tide-pools-saved-1-handmade.json?raw'
import resumeTideSaved2 from '../../../../fixtures/business/media/compose-resume-tide-pools-saved-2-handmade.json?raw'
import resumeTideSaved3 from '../../../../fixtures/business/media/compose-resume-tide-pools-saved-3-handmade.json?raw'
import resumeTideSaved4 from '../../../../fixtures/business/media/compose-resume-tide-pools-saved-4-handmade.json?raw'
import resumeTideSaved5 from '../../../../fixtures/business/media/compose-resume-tide-pools-saved-5-handmade.json?raw'
import resumeTideStopped2 from '../../../../fixtures/business/media/compose-resume-tide-pools-stopped-2-handmade.json?raw'
import resumeTideStopped3 from '../../../../fixtures/business/media/compose-resume-tide-pools-stopped-3-handmade.json?raw'
import resumeTideStopped4 from '../../../../fixtures/business/media/compose-resume-tide-pools-stopped-4-handmade.json?raw'
import renderEdit from '../../../../fixtures/business/media/compose-render-tides-edit-handmade.jsonl?raw'
import renderEditSaved1 from '../../../../fixtures/business/media/compose-render-tides-edit-saved-1-handmade.json?raw'
import renderEditSaved2 from '../../../../fixtures/business/media/compose-render-tides-edit-saved-2-handmade.json?raw'
import renderEditSaved3 from '../../../../fixtures/business/media/compose-render-tides-edit-saved-3-handmade.json?raw'
import renderEditSaved4 from '../../../../fixtures/business/media/compose-render-tides-edit-saved-4-handmade.json?raw'
import renderEditStopped1 from '../../../../fixtures/business/media/compose-render-tides-edit-stopped-1-handmade.json?raw'
import renderEditStopped2 from '../../../../fixtures/business/media/compose-render-tides-edit-stopped-2-handmade.json?raw'
import renderEditStopped3 from '../../../../fixtures/business/media/compose-render-tides-edit-stopped-3-handmade.json?raw'
import type { Skipped } from '../../../business/media/MediaRecords'
import type { Answer, Rule, Rules } from '../../../infrastructure/process/StandInChild'
import type { FolderFile, FolderWrite, Folders } from '../../../infrastructure/process/StandInFolders'

/** `~/Movies/Apogee`, where compositions are written and read: the stand-in's home, as the clips' folder is (design 1k). */
export const moviesFolder = '/Users/someone/Movies/Apogee'
const inMovies = (name: string) => `${moviesFolder}/${name}`

/** The store's voice model the new composition names first (PyTorch), and the voice the window's preferences start from. */
export const voiceModel = 'Kokoro-82M/voice/5f3a2c91d0e4'

/** The compositions, by their video: the old prototype's four (each a hand-made `video.project` in the CLI's shape), and the recorded Tides, made (1l). */
export const projects = {
  heatPump: inMovies('heat-pump.mp4'),
  nightMarket2: inMovies('night-market-2.mp4'),
  nightMarket: inMovies('night-market.mp4'),
  tidePools: inMovies('tide-pools.mp4'),
  tides: inMovies('tides.mp4'),
  tidesEdit: inMovies('tides-edit.mp4'),
  /** The new composition's video: the Mac's free name for the recorded brief. */
  fresh: inMovies('the-tides-twice-a-day.mp4'),
} as const

/** The brief the recorded plan was made from (`compose-planned.jsonl`): where a new composition starts, since nothing else was recorded. */
export const recordedBrief = 'the tides, twice a day'
/** The instruction the recorded edit preview was planned for (`compose-edit-preview.json`). */
export const recordedEdit = 'make the second scene the moon'
/** The tide project's brief: its resume runs it. */
const tideBrief = 'A gentle 40-second video for children aged five to eight about what lives in rock pools, and how to look without harming anything.'

/** Where the old frame's player stands on `compose`: the heat pump paused at 9.6 s. */
export const heatPumpFrame = 9.6

/** A project's files in the folder: its video once made, its captions beside it, and its project. */
function projectFiles(output: string, text: string, made: boolean, captions: boolean): FolderFile[] {
  const name = output.slice(output.lastIndexOf('/') + 1)
  return [...(made ? [{ name }] : []), ...(captions ? [{ name: name.replace(/\.mp4$/, '.srt') }] : []), { name: `${name}.json`, text }]
}

/** The old prototype's four compositions in `~/Movies/Apogee`: three made, the tide stopped partway with no video yet. */
export function composeFiles(): FolderFile[] {
  return [
    ...projectFiles(projects.heatPump, heatPump, true, true),
    ...projectFiles(projects.nightMarket2, nightMarket2, true, true),
    ...projectFiles(projects.nightMarket, nightMarket, true, true),
    ...projectFiles(projects.tidePools, tidePools, false, false),
  ]
}

/** The recorded Tides, made: the parent `compose:edit` previews an edit of and `compose:differs` renders (1l's hand-made done version of `compose-edit-parent.json`). */
export function tidesFiles(): FolderFile[] {
  return projectFiles(projects.tides, tidesDone, true, true)
}

/** `folders` with `files` added to `~/Movies/Apogee`: the compositions beside the clips. */
export function withComposeFiles(folders: Folders, files: readonly FolderFile[] = composeFiles()): Folders {
  return { ...folders, [moviesFolder]: [...(folders[moviesFolder] ?? []), ...files] }
}

/**
 * The documents a scan of the person's folders passed over (the old
 * `comp.skipped`): a hand-made list in the Mac scan's `Skipped` shape, which
 * the stand-in serves, since no CLI command records what a folder scan
 * passed over -- and a file that is not JSON in `~/Movies/Apogee` would be
 * passed over by Clips too, which the old drew without one (design 1l).
 */
export function passedOver(): Skipped[] {
  return (JSON.parse(passedOverList) as { skipped: Skipped[] }).skipped
}

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })
/** With the 52b stream asked for before the brief's `--`, as the place asks for it (the Mac's `Argv.adding`). */
function streamed(args: readonly string[]): string[] {
  const at = args.indexOf('--')
  return at < 0 ? [...args, '--output-format', 'stream-json'] : [...args.slice(0, at), '--output-format', 'stream-json', ...args.slice(at)]
}

/**
 * Each step's project as the composer saves it, after how many of the
 * stream's lines (the step's last: its narration's segment, its clip's
 * assembly, or the document; a new render's first, nothing made, after none
 * -- as the run starts, the plan beside the video before anything renders),
 * and what a stop there leaves; the last, done, has no stop.
 */
function saves(after: readonly number[], saved: readonly string[], stopped: readonly string[]): FolderWrite[] {
  const name = (text: string) => {
    const output = (JSON.parse(text) as { output: string }).output
    return `${output.slice(output.lastIndexOf('/') + 1)}.json`
  }
  return saved.map((text, index) => {
    const stop = stopped[index]
    return { after: after[index] ?? 0, folder: moviesFolder, file: { name: name(text), text }, ...(stop === undefined ? {} : { stopped: { name: name(stop), text: stop } }) }
  })
}

/** The new composition's arguments, as the form makes them for the recorded brief: its voice model named, the store holding two. */
export const freshArguments = ['video', 'compose', '-o', projects.fresh, '--voice-model', voiceModel, '--', recordedBrief]
/** The plan: the same, a dry run. */
export const planArguments = ['video', 'compose', '-o', projects.fresh, '--voice-model', voiceModel, '--dry-run', '--', recordedBrief]
/** The tide resumed: its own command again, which renders only what its project lacks (the Mac's `resumeArguments`). */
export const tideResumeArguments = ['video', 'compose', '-o', projects.tidePools, '--', tideBrief]
/** The recorded edit of Tides, as Render runs it and its preview's dry run. */
export const editArguments = ['video', 'compose', '--from', projects.tides, '--edit', recordedEdit, '-o', projects.tidesEdit]
export const previewArguments = [...editArguments, '--dry-run', '--output-format', 'json']
/** Tides' scene 2 re-rolled alone, on paper (`compose-edit-redo.json`), and scene 1 re-rolled beside the recorded instruction (`compose-edit-reroll-preview.json`): the CLI's recorded re-roll previews. */
export const redoPreviewArguments = ['video', 'compose', '--from', projects.tides, '--redo-scene', '2', '-o', projects.tidesEdit, '--dry-run', '--output-format', 'json']
export const rerollPreviewArguments = ['video', 'compose', '--from', projects.tides, '--edit', recordedEdit, '--redo-scene', '1', '-o', projects.tidesEdit, '--dry-run', '--output-format', 'json']

/** How long the edit's preview plans: the old prototype's 4 ticks (its one line, held two ticks more). */
const previewTicks = 2

/**
 * The compositions' answers on every preset (design 1l), so the tab opens
 * whole from any screen: the recorded plan of the recorded brief
 * (`compose-planned.jsonl`), the plan rendered, the tide resumed, the
 * recorded edit of Tides previewed (`compose-edit-preview.json`) and
 * rendered, and the two recorded re-rolls of Tides previewed on paper
 * (scene 2 alone, `compose-edit-redo.json`; scene 1 beside the recorded
 * instruction, `compose-edit-reroll-preview.json`), neither rendered, since
 * nothing recorded their renders -- each render a hand-made stream in the 52b shape with the
 * project the composer saves after every step (`writes`), so the screen
 * reads its progress beside the video, as the Mac does. Each is answered for
 * its exact arguments.
 */
export function composePlaceRules(): Required<Pick<Rules, 'jobs'>> {
  const render = (args: readonly string[], answer: Answer): Rule => ({ arguments: streamed(args), exact: true, answer })
  return {
    jobs: [
      render(planArguments, { stdout: planned }),
      render(freshArguments, { stdout: renderNew, writes: saves([0, 6, 9, 19, 28, 31], [renderNewSaved1, renderNewSaved2, renderNewSaved3, renderNewSaved4, renderNewSaved5, renderNewSaved6], [renderNewStopped1, renderNewStopped2, renderNewStopped3, renderNewStopped4, renderNewStopped5]) }),
      render(tideResumeArguments, { stdout: resumeTide, writes: saves([2, 12, 21, 30, 33], [resumeTideSaved1, resumeTideSaved2, resumeTideSaved3, resumeTideSaved4, resumeTideSaved5], [tidePools, resumeTideStopped2, resumeTideStopped3, resumeTideStopped4]) }),
      { arguments: previewArguments, exact: true, answer: { stdout: editPreview, stderr: editPreviewErr, exitAfter: previewTicks } },
      { arguments: redoPreviewArguments, exact: true, answer: { stdout: editRedo, stderr: editRedoErr, exitAfter: previewTicks } },
      { arguments: rerollPreviewArguments, exact: true, answer: { stdout: editReroll, exitAfter: previewTicks } },
      render(editArguments, { stdout: renderEdit, writes: saves([0, 6, 17, 20], [renderEditSaved1, renderEditSaved2, renderEditSaved3, renderEditSaved4], [renderEditStopped1, renderEditStopped2, renderEditStopped3]) }),
    ],
  }
}

/**
 * The compositions' world for the module's own previews and tests: the
 * place's answers and what it reads of the store -- the video world's
 * `models list` (the Video presets' store, two Kokoro voice sets beside Wan
 * 2.2), the default pipeline, each voice set's `models info` and `check`,
 * the recordings Settings and Video answer the same reads with on every
 * preset. A place's preview may not import another place, so it names
 * them itself.
 */
export function composeWorld(): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  return {
    ...composePlaceRules(),
    reads: [read(['models', 'list'], modelsListVideo), read(['check'], checkCaptionsSkipped), read(['models', 'info', voiceModel], modelsStoredInfoVoice), read(['models', 'info', 'Kokoro-82M/mlx/6f708192a3b4'], storeInfoKindVoice)],
    commands: [read(['config', 'get', 'models.default_video'], mediaDefaultVideo)],
  }
}

/** The capture store's `models list`, with its two Kokoro sets (`compose-voices-list.json`): the voice-model picker's own recording, for a preview. */
export const capturedVoiceList = composeVoicesList
