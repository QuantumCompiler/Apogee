import type { Preset } from '../../../infrastructure/process/Preset'
import { composeFiles, composePlaceRules, tidesFiles, withComposeFiles } from '../../compose/Previews/ComposeWorlds'
import { consoleRules } from '../../console/Previews/ConsolePreset'
import { graphPlaceRules } from '../../graph/Previews/GraphWorlds'
import { knowledgePlaceRules } from '../../knowledge/Previews/KnowledgeWorlds'
import { libraryPlaceRules, playTicks, withLibraryFolders, withLibraryWorld } from '../../library/Previews/LibraryWorlds'
import type { LibraryPreset } from '../../library/Previews/LibraryWorlds'
import { saveTicks } from '../../library/ViewModels/DefinitionEditor'
import { modelsPlaceRules, withModelsWorld } from '../../models/Previews/ModelsWorlds'
import type { ModelsWorld } from '../../models/Previews/ModelsWorlds'
import { monitorPreset } from '../../monitor/Previews/MonitorPreset'
import { reviewPlaceRules, withReviewFolders } from '../../review/Previews/ReviewWorlds'
import { fixLines, settingsPlaceRules, withSettingsWorld } from '../../settings/Previews/SettingsWorlds'
import type { SettingsWorld } from '../../settings/Previews/SettingsWorlds'
import type { PresetOptions } from '../../monitor/Previews/MonitorPreset'
import {
  chatCompactingPreset,
  chatCrashPreset,
  chatEndedPreset,
  chatNewPreset,
  chatNotSentPreset,
  chatPlayPreset,
  chatPreset,
  chatPrivatePreset,
  chatPrPreset,
  chatRestartingPreset,
  chatRetryPreset,
  chatSuitePreset,
  chatWalPreset,
  streamingRules,
} from '../../session/Previews/SessionPresets'
import { executePreset } from '../../sessions/Previews/ExecutePreset'
import { suitesPlaceRules } from '../../suites/Previews/SuitesWorlds'
import { tasksPlaceRules, tasksPresets, withTasksWorld } from '../../tasks/Previews/TasksWorlds'
import { trainingPlaceRules, withTrainingWorld } from '../../training/Previews/TrainingWorlds'
import { videoFolders, videoPlaceRules, withVideoWorld } from '../../video/Previews/VideoWorlds'
import { voiceFolders, voicePlaceRules, withVoiceWorld } from '../../voice/Previews/VoiceWorlds'
import type { TrainingWorld } from '../../training/Previews/TrainingWorlds'
import { withLibraryRules } from '../../sessions/Previews/LibraryRules'
import type { StartRequest } from '../../shell/ViewModels/Routing'

/**
 * `monitor` (1e) over the Streaming session at `chat`'s frame, as the old
 * `applyStart('monitor')` sets `progress: 30` (design 1v): the monitor's
 * reads and the Streaming setup's rules, composed here, since `monitor` may
 * not import `session`.
 */
function monitorOverStreaming(options: PresetOptions): Preset {
  const monitor = monitorPreset(options)
  const streaming = streamingRules('chat')
  return { ...monitor, rules: { ...monitor.rules, reads: [...(monitor.rules.reads ?? []), ...streaming.reads], commands: streaming.commands, chats: streaming.chats } }
}

/**
 * `console` (design 1g): the console over the Streaming session at its
 * starting frame, as the old `applyStart('console')` leaves `progress` at the
 * state's own 15 -- 1v's setup, `streamingRules('start')` -- composed here,
 * since `console` may not import `session`. Its console's answers are every
 * preset's (`consoling`).
 */
function consolePreset({ still, apogee }: PresetOptions): Preset {
  // Ten start ticks: models list's seven lines play and its run ends within them, so `reset` runs after it.
  return { rules: streamingRules('start'), route: 'console', clock: { start: 10, still }, apogee }
}

/**
 * A Models preset (design 1h): the place over its world -- the store, the
 * snapshots before conversion, or the fresh store a pull lands in -- with
 * the Streaming session at its starting frame behind it, as the old frames
 * keep `taskgroup` open there, composed here since `models` may not import
 * `session`. `start` is how many ticks in a still frame stands:
 * `models:downloads`' conversion mid-run at 10.
 */
function modelsPreset(route: string, world: ModelsWorld, start = 1): (options: PresetOptions) => Preset {
  return ({ still, apogee }) => ({ rules: withModelsWorld(streamingRules('start'), world), route, clock: { start, still }, apogee })
}

/**
 * A Settings preset (design 1i): the window over the Streaming session at its
 * starting frame, as the old frames keep `taskgroup` at its `progress: 15`
 * (1v's setup, `streamingRules('start')`, composed here since `settings`
 * may not import `session`), the window's world beside it. `start` is how
 * many ticks in a still frame stands: `fixfailed`'s run of `check --fix` has
 * ended by then, its toast still showing.
 */
function settingsPreset(sub: string, world: SettingsWorld = 'default', start = 1): (options: PresetOptions) => Preset {
  return ({ still, apogee }) => ({ rules: withSettingsWorld(streamingRules('start'), world), route: `settings:${sub}`, clock: { start, still }, apogee })
}

/**
 * A Training preset (design 1j): the place over its world, the Streaming
 * session at its starting frame behind it (as the Models and Settings
 * presets keep it, composed here since `training` may not import
 * `session`). `start` is how many ticks in a still frame stands: `training`'s
 * run at `train-run.err`'s fourth line, `training:pipelines`' `skills` run at
 * stage 2, iteration 2 (its stream's 24th line).
 */
function trainingPreset(sub: string, world: TrainingWorld, start = 1): (options: PresetOptions) => Preset {
  return ({ still, apogee }) => ({ rules: withTrainingWorld(streamingRules('start'), world), route: sub === '' ? 'training' : `training:${sub}`, clock: { start, still }, apogee })
}

/**
 * A Video preset (design 1k): the place over its world -- the store's
 * `models list` with the Wan 2.2 pipeline beside its MLX conversion, and
 * `~/Movies/Apogee`'s five records -- the Streaming session at its starting
 * frame behind it (as the Models, Settings and Training presets keep it,
 * composed here since `video` may not import `session`). `start` is how many
 * ticks in a still frame stands: `video:run`'s run at step 38 of 50 (its
 * stream's 43rd line), and `consent`'s and `video:install`'s refusal played
 * (three lines), `video:install`'s install four ticks in.
 */
function videoPreset(route: string, start = 1): (options: PresetOptions) => Preset {
  // Its compositions beside its clips (1l), so the Compositions tab counts them, as the old tab says 4.
  return ({ still, apogee }) => ({ rules: withVideoWorld(streamingRules('start')), route, clock: { start, still }, apogee, folders: withComposeFiles(videoFolders()) })
}

/**
 * A Compose preset (design 1l): Video's Compositions tab over the video
 * world, the Streaming session at its starting frame behind it, as the Video
 * presets keep it (composed here, since neither `compose` nor `video` may
 * import `session`), and `~/Movies/Apogee`'s clips and compositions --
 * with the recorded Tides, made, where an edit of it is previewed
 * (`compose:edit`) or rendered (`compose:differs`). `start` is how many
 * ticks in a still frame stands: `compose:edit`'s preview landed (4),
 * `compose:new`'s plan (its stream's ten lines and its end),
 * `compose:differs`' preview and then its render (19 lines and its end).
 */
function composePreset(route: string, start = 1, tides = false): (options: PresetOptions) => Preset {
  return ({ still, apogee }) => ({ rules: withVideoWorld(streamingRules('start')), route, clock: { start, still }, apogee, folders: withComposeFiles(videoFolders(), tides ? [...composeFiles(), ...tidesFiles()] : composeFiles()) })
}

/**
 * The Voice preset (design 1m): the place over its world -- the store's
 * `models list` with its one voice model (`models-list-voice-handmade.json`:
 * `models-stored-list.json`'s media rows beside the sessions' backends) and
 * that model's `models info`, and `~/Music/Apogee`'s two recorded speeches
 * beside the other presets' folders -- the Streaming session at its starting
 * frame behind it, as the Video
 * presets keep it (composed here, since `voice` may not import `session`).
 */
function voicePreset({ still, apogee }: PresetOptions): Preset {
  return { rules: withVoiceWorld(streamingRules('start')), route: 'voice', clock: { start: 1, still }, apogee, folders: voiceFolders(withComposeFiles(videoFolders())) }
}

/**
 * A Library preset (design 1n): the place over its world -- every preset's
 * Library answers (`symphonies show` refusing notes-digest while it holds
 * the unknown key: `library:refused`'s save) and the preset's own `mcp
 * presets` (the old frames' two) -- the Streaming session at
 * its starting frame behind it, as the Voice preset keeps it (composed here,
 * since `library` may not import `session`). `start` is how many ticks in a
 * still frame stands: `library:symphonies`' recorded play to its end
 * (`playTicks`), `library:refused`'s save to apogee's word (`saveTicks`).
 */
function libraryPreset(route: LibraryPreset, start = 1): (options: PresetOptions) => Preset {
  return ({ still, apogee }) => ({ rules: withLibraryWorld(streamingRules('start'), route), route, clock: { start, still }, apogee })
}

/**
 * A Review preset (design 1n): the place over every preset's Review answers,
 * the Streaming session at its starting frame behind it. `start` is how many
 * ticks in a still frame stands: the dry run's one line and its end.
 */
function reviewPreset(route: string, start = 1): (options: PresetOptions) => Preset {
  return ({ still, apogee }) => ({ rules: streamingRules('start'), route, clock: { start, still }, apogee })
}

/**
 * A Knowledge or Code graph preset (design 1o): the place over every preset's
 * answers, the Streaming session at its starting frame behind it, as the
 * Review presets keep it (composed here, since neither may import
 * `session`). Nothing in either plays on the clock: one start tick.
 */
function placePreset(route: string): (options: PresetOptions) => Preset {
  return ({ still, apogee }) => ({ rules: streamingRules('start'), route, clock: { start: 1, still }, apogee })
}

/**
 * The Suites preset (design 1p): the place over every preset's answers, the
 * Streaming session at its starting frame behind it, as the Knowledge and
 * Code graph presets keep it (composed here, since `suites` may not import
 * `session`). Nothing plays on the clock: one start tick.
 */
function suitesPreset({ still, apogee }: PresetOptions): Preset {
  return { rules: streamingRules('start'), route: 'suites', clock: { start: 1, still }, apogee }
}

/**
 * A Tasks or New task preset (design 1p): the place over its world -- its
 * ledger, the hand-made `task list` of recorded tasks, or the recorded
 * ledger of 60 -- the Streaming session at its starting frame behind it,
 * composed here since `tasks` may not import `session`. `start` is how many
 * ticks in a still frame stands: the recorded say hello run 16 lines in, in
 * round 1, its answer streaming (`task-run.jsonl`); the recorded uses run
 * played to its end (34 lines and its end); a resume one tick in, starting up.
 */
function tasksPreset(route: string, start = 16): (options: PresetOptions) => Preset {
  const world = tasksPresets[route] ?? 'ledger'
  return ({ still, apogee }) => ({ rules: withTasksWorld(streamingRules('start'), world), route, clock: { start, still }, apogee })
}

/**
 * A preset with the console's answers (design 1g): the console opens over
 * every screen (⇧⌘K), so every preset answers its lines -- `consoleRules`,
 * on the console's own child (`Preset.console`), a line nobody recorded
 * saying the old "Done." there and nowhere else.
 */
function consoling(build: (options: PresetOptions) => Preset): (options: PresetOptions) => Preset {
  return (options) => ({ ...build(options), console: consoleRules() })
}

/**
 * A preset with the Sessions place's reads beside its own (design 1g), since
 * every screen's sidebar lists the recorded chats, the Models place's
 * (design 1h), since it opens from every screen, and the console's answers.
 */
function listing(build: (options: PresetOptions) => Preset): (options: PresetOptions) => Preset {
  return modelling((options) => {
    const preset = build(options)
    return { ...preset, rules: withLibraryRules(preset.rules) }
  })
}

/**
 * A preset with the Models place's reads (design 1h), the Settings window's
 * (design 1i), the Training place's (design 1j), the Video place's (design
 * 1k: its runs, the install and `~/Movies/Apogee`'s records, unless the
 * preset names its own folders) and the compositions' (design 1l: their
 * plan, renders and preview, and the old four projects beside the clips)
 * and Voice's (design 1m: the sample spoken, and `~/Music/Apogee`'s two
 * recorded speeches beside the preset's folders), Library's (design 1n: its
 * lists, `config path`, the recorded play, the removal of `code`, and the
 * data directory's two editor files) and Review's (design 1n: the targets,
 * the skills, the recorded dry run, the starting target added, and the
 * sweep the CLI kept), Knowledge's (design 1o: the records, the old
 * question, the rejected record's raw conversation, the collections, their
 * pages and notes' search) and the Code graph's (design 1o: the demo and
 * notes graphs, their pictures, report and explain, and the More menu's
 * commands), Suites' (design 1p: the recorded suites, their footprints and
 * the default's verb) and Tasks' (design 1p: the ledger, every recorded
 * task's page, the recorded runs and resume with their stops, the two
 * refusals and the recipes) before its own, so a preset's own answer to the
 * same arguments is the one given: all twelve open from every screen.
 */
function modelling(build: (options: PresetOptions) => Preset): (options: PresetOptions) => Preset {
  return consoling((options) => {
    const preset = build(options)
    const models = modelsPlaceRules()
    const settings = settingsPlaceRules()
    const training = trainingPlaceRules()
    const video = videoPlaceRules()
    const compose = composePlaceRules()
    const voice = voicePlaceRules()
    const library = libraryPlaceRules()
    const review = reviewPlaceRules()
    // Knowledge's and the Code graph's (1o): ⌘8 and ⌘5 open them from every screen.
    const knowledge = knowledgePlaceRules()
    const graph = graphPlaceRules()
    // Suites' and Tasks' (1p): ⌘3 and ⌘4 open them from every screen, Tasks over the ledger.
    const suites = suitesPlaceRules()
    const tasks = tasksPlaceRules()
    return {
      ...preset,
      folders: withReviewFolders(withLibraryFolders(voiceFolders(preset.folders ?? withComposeFiles(videoFolders())))),
      rules: {
        ...preset.rules,
        reads: [...(models.reads ?? []), ...settings.reads, ...(training.reads ?? []), ...video.reads, ...library.reads, ...review.reads, ...knowledge.reads, ...graph.reads, ...suites.reads, ...tasks.reads, ...(preset.rules.reads ?? [])],
        commands: [...(models.commands ?? []), ...settings.commands, ...(training.commands ?? []), ...video.commands, ...library.commands, ...review.commands, ...knowledge.commands, ...graph.commands, ...suites.commands, ...tasks.commands, ...(preset.rules.commands ?? [])],
        jobs: [...settings.jobs, ...(training.jobs ?? []), ...video.jobs, ...compose.jobs, ...voice.jobs, ...library.jobs, ...review.jobs, ...graph.jobs, ...tasks.jobs, ...(preset.rules.jobs ?? [])],
      },
    }
  })
}

/**
 * The start presets the built file opens at (design 1e): the old prototype's
 * `start` options, each rebuilt on recordings by the item that ports its
 * place. 1e's is `monitor`; 1u's are the session's four plain ones; 1v's the
 * session's scenarios and states, in the old `data-props` order; 1g's
 * `execute` and `console`; 1h's the nine Models presets; 1i's the old nine
 * Settings presets in the old order and its four new ones, one a pane that
 * had none; 1j's the old nine Training presets in the old order and its new
 * `training:regimes`; 1k's the four Video presets in the old order; 1l's
 * the five Compose presets in the old order, `compose:differs` among them;
 * 1m's `voice`; 1n's six Library and four Review presets in the old
 * order; 1o's three Knowledge and three Code graph presets in the old order,
 * `knowledge:mixed` and `graph:refused` among them; 1p's Suites preset and
 * its eight Tasks and New task presets in the old order; each feature item
 * adds its own here. Every one
 * carries the Sessions place's reads, since the list shows on every screen
 * (`execute`'s own), and the console's answers, since it opens over every one.
 */
export const presets: Readonly<Record<string, (options: PresetOptions) => Preset>> = {
  monitor: listing(monitorOverStreaming),
  chat: listing(chatPreset),
  'chat:retry': listing(chatRetryPreset),
  'chat:wal': listing(chatWalPreset),
  'chat:crash': listing(chatCrashPreset),
  'chat:pr': listing(chatPrPreset),
  'chat:new': listing(chatNewPreset),
  'chat:private': listing(chatPrivatePreset),
  'chat:play': listing(chatPlayPreset),
  'chat:suite': listing(chatSuitePreset),
  'chat:restarting': listing(chatRestartingPreset),
  'chat:notsent': listing(chatNotSentPreset),
  'chat:ended': listing(chatEndedPreset),
  'chat:compacting': listing(chatCompactingPreset),
  execute: modelling(executePreset),
  console: listing(consolePreset),
  models: listing(modelsPreset('models', 'store')),
  'models:downloads': listing(modelsPreset('models:downloads', 'conversions', 10)),
  'models:store': listing(modelsPreset('models:store', 'conversions')),
  'models:convert': listing(modelsPreset('models:convert', 'conversions')),
  'models:register': listing(modelsPreset('models:register', 'store')),
  'models:roles': listing(modelsPreset('models:roles', 'store')),
  'models:delete': listing(modelsPreset('models:delete', 'store')),
  'models:notran': listing(modelsPreset('models:notran', 'store')),
  download: listing(modelsPreset('download', 'fresh')),
  'settings:general': listing(settingsPreset('general')),
  'settings:appearance': listing(settingsPreset('appearance')),
  'settings:media': listing(settingsPreset('media')),
  'settings:backends': listing(settingsPreset('backends')),
  'settings:fixing': listing(settingsPreset('fixing')),
  'settings:fixfailed': listing(settingsPreset('fixfailed', 'fixfailed', fixLines('fixfailed') + 2)),
  'settings:permissions': listing(settingsPreset('permissions')),
  'settings:nosearch': listing(settingsPreset('nosearch', 'nosearch')),
  'settings:xcode': listing(settingsPreset('xcode', 'xcode')),
  'settings:roles': listing(settingsPreset('roles')),
  'settings:web': listing(settingsPreset('web')),
  'settings:memory': listing(settingsPreset('memory')),
  'settings:keyboard': listing(settingsPreset('keyboard')),
  training: listing(trainingPreset('', 'recorded', 4)),
  'training:datasets': listing(trainingPreset('datasets', 'recorded')),
  'training:pipelines': listing(trainingPreset('pipelines', 'pipelines', 24)),
  'training:cycle': listing(trainingPreset('cycle', 'cycle')),
  'training:stalelock': listing(trainingPreset('stalelock', 'stalelock')),
  'training:unreadable': listing(trainingPreset('unreadable', 'unreadable')),
  'training:cyclepass': listing(trainingPreset('cyclepass', 'stalelock')),
  'training:passrunning': listing(trainingPreset('passrunning', 'passrunning')),
  'training:resumeheld': listing(trainingPreset('resumeheld', 'resumeheld')),
  'training:regimes': listing(trainingPreset('regimes', 'recorded', 4)),
  video: listing(videoPreset('video')),
  'video:run': listing(videoPreset('video:run', 43)),
  'video:install': listing(videoPreset('video:install', 4)),
  consent: listing(videoPreset('consent', 4)),
  compose: listing(composePreset('compose')),
  'compose:edit': listing(composePreset('compose:edit', 5, true)),
  'compose:new': listing(composePreset('compose:new', 12)),
  'compose:tide': listing(composePreset('compose:tide')),
  'compose:differs': listing(composePreset('compose:differs', 30, true)),
  voice: listing(voicePreset),
  library: listing(libraryPreset('library')),
  'library:symphonies': listing(libraryPreset('library:symphonies', playTicks)),
  'library:mcp': listing(libraryPreset('library:mcp')),
  'library:unsaved': listing(libraryPreset('library:unsaved')),
  'library:refused': listing(libraryPreset('library:refused', saveTicks)),
  'library:image': listing(libraryPreset('library:image')),
  review: listing(reviewPreset('review', 2)),
  'review:post': listing(reviewPreset('review:post', 2)),
  'review:targets': listing(reviewPreset('review:targets')),
  'review:skills': listing(reviewPreset('review:skills')),
  knowledge: listing(placePreset('knowledge')),
  'knowledge:collections': listing(placePreset('knowledge:collections')),
  'knowledge:mixed': listing(placePreset('knowledge:mixed')),
  graph: listing(placePreset('graph')),
  'graph:prose': listing(placePreset('graph:prose')),
  'graph:refused': listing(placePreset('graph:refused')),
  suites: listing(suitesPreset),
  tasks: listing(tasksPreset('tasks')),
  'tasks:denied': listing(tasksPreset('tasks:denied', 36)),
  'tasks:more': listing(tasksPreset('tasks:more')),
  'tasks:resuming': listing(tasksPreset('tasks:resuming', 1)),
  'tasks:refused': listing(tasksPreset('tasks:refused')),
  'tasks:resumerefused': listing(tasksPreset('tasks:resumerefused', 1)),
  newtask: listing(tasksPreset('newtask')),
  'newtask:recipe': listing(tasksPreset('newtask:recipe')),
}

/** The preset opened with no `start`, or an unknown one: `chat`, the old `data-props` default (1v; 1e's interim default was the monitor). */
export const defaultStart = 'chat'

/** The preset a page's start request opens, and whether its `start` named a known one. */
export function presetFor(request: StartRequest): { readonly preset: Preset; readonly known: boolean } {
  const named = request.start === undefined ? undefined : presets[request.start]
  const build = named ?? presets[defaultStart]
  if (build === undefined) throw new Error(`the default start ${defaultStart} is not a preset`)
  return { preset: build({ still: request.still, apogee: request.apogee }), known: request.start === undefined || named !== undefined }
}
