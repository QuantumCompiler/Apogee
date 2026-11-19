import type { Preset } from '../../../infrastructure/process/Preset'
import { consoleRules } from '../../console/Previews/ConsolePreset'
import { monitorPreset } from '../../monitor/Previews/MonitorPreset'
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
 * A preset with the console's answers (design 1g): the console opens over
 * every screen (⇧⌘K), so every preset answers its lines -- `consoleRules`,
 * on the console's own child (`Preset.console`), a line nobody recorded
 * saying the old "Done." there and nowhere else.
 */
function consoling(build: (options: PresetOptions) => Preset): (options: PresetOptions) => Preset {
  return (options) => ({ ...build(options), console: consoleRules() })
}

/** A preset with the Sessions place's reads beside its own (design 1g), since every screen's sidebar lists the recorded chats, and the console's answers. */
function listing(build: (options: PresetOptions) => Preset): (options: PresetOptions) => Preset {
  return consoling((options) => {
    const preset = build(options)
    return { ...preset, rules: withLibraryRules(preset.rules) }
  })
}

/**
 * The start presets the built file opens at (design 1e): the old prototype's
 * `start` options, each rebuilt on recordings by the item that ports its
 * place. 1e's is `monitor`; 1u's are the session's four plain ones; 1v's the
 * session's scenarios and states, in the old `data-props` order; 1g's
 * `execute` and `console`; each feature item adds its own here. Every one
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
  execute: consoling(executePreset),
  console: listing(consolePreset),
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
