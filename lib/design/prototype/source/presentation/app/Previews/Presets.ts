import type { Preset } from '../../../infrastructure/process/Preset'
import { consoleRules } from '../../console/Previews/ConsolePreset'
import { modelsPlaceRules, withModelsWorld } from '../../models/Previews/ModelsWorlds'
import type { ModelsWorld } from '../../models/Previews/ModelsWorlds'
import { monitorPreset } from '../../monitor/Previews/MonitorPreset'
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
 * A preset with the Models place's reads (design 1h) and the Settings
 * window's (design 1i) before its own, so a preset's own answer to the same
 * arguments is the one given: both open from every screen.
 */
function modelling(build: (options: PresetOptions) => Preset): (options: PresetOptions) => Preset {
  return consoling((options) => {
    const preset = build(options)
    const models = modelsPlaceRules()
    const settings = settingsPlaceRules()
    return {
      ...preset,
      rules: {
        ...preset.rules,
        reads: [...(models.reads ?? []), ...settings.reads, ...(preset.rules.reads ?? [])],
        commands: [...(models.commands ?? []), ...settings.commands, ...(preset.rules.commands ?? [])],
        jobs: [...settings.jobs, ...(preset.rules.jobs ?? [])],
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
 * had none; each feature item adds its own here. Every one
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
