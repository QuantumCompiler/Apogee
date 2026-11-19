import type { Preset } from '../../../infrastructure/process/Preset'
import { monitorPreset } from '../../monitor/Previews/MonitorPreset'
import type { PresetOptions } from '../../monitor/Previews/MonitorPreset'
import { chatNewPreset, chatPlayPreset, chatPrivatePreset, chatSuitePreset } from '../../session/Previews/SessionPresets'
import type { StartRequest } from '../../shell/ViewModels/Routing'

/**
 * The start presets the built file opens at (design 1e): the old prototype's
 * `start` options, each rebuilt on recordings by the item that ports its
 * place. 1e's is `monitor`; 1u's are the session's four plain ones; each
 * feature item adds its own here.
 */
export const presets: Readonly<Record<string, (options: PresetOptions) => Preset>> = {
  monitor: monitorPreset,
  'chat:new': chatNewPreset,
  'chat:private': chatPrivatePreset,
  'chat:play': chatPlayPreset,
  'chat:suite': chatSuitePreset,
}

/**
 * The preset opened with no `start`, or an unknown one: the monitor, until 1v
 * ports the Streaming session and restores `chat`, the old `data-props`
 * default, so a double-click shows a working screen.
 */
export const defaultStart = 'monitor'

/** The preset a page's start request opens, and whether its `start` named a known one. */
export function presetFor(request: StartRequest): { readonly preset: Preset; readonly known: boolean } {
  const named = request.start === undefined ? undefined : presets[request.start]
  const build = named ?? presets[defaultStart]
  if (build === undefined) throw new Error(`the default start ${defaultStart} is not a preset`)
  return { preset: build({ still: request.still, apogee: request.apogee }), known: request.start === undefined || named !== undefined }
}
