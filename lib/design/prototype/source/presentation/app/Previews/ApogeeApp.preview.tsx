import { useState } from 'react'
import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { ApogeeApp } from '../Views/ApogeeApp'
import { AppViewModel } from '../ViewModels/AppViewModel'
import { modeRules } from './ModeRules'
import { presetFor } from './Presets'

/** The whole window at a start preset, still, as the built file opens it with `?start=<preset>&still=true&apogee=<mode>` (design 1e). */
function useApp(start: string, apogee: ApogeeMode) {
  const [app] = useState(() => new AppViewModel(presetFor({ start, still: true, apogee }).preset, modeRules))
  return app
}

/** `monitor`: the monitor over Sessions, the strip open, on the recorded reading. */
export const Monitor = () => <ApogeeApp model={useApp('monitor', 'installed')} />

/** `monitor` with no binary found. */
export const MonitorMissing = () => <ApogeeApp model={useApp('monitor', 'missing')} />

/** `monitor` with a config the CLI refuses. */
export const MonitorConfigTypo = () => <ApogeeApp model={useApp('monitor', 'config-typo')} />
