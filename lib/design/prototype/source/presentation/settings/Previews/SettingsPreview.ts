import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../infrastructure/process/StandInChild'
import { KeyMap } from '../../shell/ViewModels/KeyMap'
import type { BoundKey } from '../../shell/ViewModels/KeyMap'
import { ThemeService } from '../../styles/Appearance/ThemeService'
import { SettingsViewModel } from '../ViewModels/SettingsViewModel'
import type { SettingsPane, SettingsPreferences } from '../ViewModels/SettingsViewModel'
import { settingsPreferences, settingsPreviewRules } from './SettingsWorlds'
import type { SettingsWorld } from './SettingsWorlds'

/** How a preview's window is set up: its world, its mode, the pane shown, the keys bound, the app's preferences, and what the person does once it has read. */
export interface SettingsPreviewOptions {
  readonly world?: SettingsWorld | Rules
  readonly apogee?: ApogeeMode
  readonly pane?: SettingsPane
  readonly keys?: readonly BoundKey[]
  readonly preferences?: SettingsPreferences
  /** Start ticks: how far in a fix stands. */
  readonly ticks?: number
  readonly then?: (settings: SettingsViewModel) => void | Promise<void>
}

/**
 * The Settings window for a preview (design 1i), the Mac app's
 * `SettingsPreview` over its documents: the stand-in answering a world's
 * recordings, a still clock, the window read, the person's act, then the
 * clock started. A place's preview may not import `app`, so it names the
 * mode rules' recording itself (1d).
 */
export function useSettingsPreview(options: SettingsPreviewOptions = {}): SettingsViewModel {
  const [model] = useState(() => {
    const rules = typeof options.world === 'object' ? options.world : settingsPreviewRules(options.world ?? 'default')
    const child = new StandInChild({ rules, apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ start: options.ticks ?? 0, still: true })
    const keys = options.keys ?? KeyMap.shell
    const settings = new SettingsViewModel(new ReadRunner(child), new JobStarter(child, clock), new ThemeService(), () => keys, options.preferences ?? settingsPreferences)
    settings.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined, settingsPreferences.passedOver)
    settings.pick(options.pane ?? 'general')
    return { settings, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.settings.appeared()
      await options.then?.(model.settings)
      model.clock.start()
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.settings
}
