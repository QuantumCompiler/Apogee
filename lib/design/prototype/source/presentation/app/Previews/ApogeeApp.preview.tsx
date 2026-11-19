import { useEffect, useState } from 'react'
import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { sessionSamples } from '../../session/Previews/SessionPresets'
import { ApogeeApp } from '../Views/ApogeeApp'
import { AppViewModel } from '../ViewModels/AppViewModel'
import { modeRules } from './ModeRules'
import { presetFor } from './Presets'

/** The whole window at a start preset, still, as the built file opens it with `?start=<preset>&still=true&apogee=<mode>` (design 1e). */
function useApp(start: string, apogee: ApogeeMode, then?: (app: AppViewModel) => void) {
  const [app] = useState(() => new AppViewModel(presetFor({ start, still: true, apogee }).preset, modeRules, undefined, sessionSamples))
  useEffect(() => {
    if (!then) return
    void app.start().then(() => then(app))
  }, [app, then])
  return app
}

/** `monitor`: the monitor over Sessions -- the session window's empty state, nothing launched -- the strip open, on the recorded reading. */
export const Monitor = () => <ApogeeApp model={useApp('monitor', 'installed')} />

/** `monitor` with no binary found. */
export const MonitorMissing = () => <ApogeeApp model={useApp('monitor', 'missing')} />

/** `monitor` with a config the CLI refuses. */
export const MonitorConfigTypo = () => <ApogeeApp model={useApp('monitor', 'config-typo')} />

/** `chat:new` (design 1u): a new chat, its empty state. */
export const ChatNew = () => <ApogeeApp model={useApp('chat:new', 'installed')} />

/** `chat:private`: a private chat, its empty state. */
export const ChatPrivate = () => <ApogeeApp model={useApp('chat:private', 'installed')} />

/** `chat:play`: the captured execute session, its play finished. */
export const ChatPlay = () => <ApogeeApp model={useApp('chat:play', 'installed')} />

/** `chat:suite`: a new chat, the suite chooser open on vast. */
export const ChatSuite = () => <ApogeeApp model={useApp('chat:suite', 'installed')} />

/** The private confirm: a private chat with a turn sent (past its recording: it runs on), left for a new session -- asked first, in the old words. */
const leavePrivate = (app: AppViewModel) => {
  app.session.setDraft('A note nobody keeps.')
  app.session.submit()
  app.leave({ type: 'new' })
}
export const EndPrivate = () => <ApogeeApp model={useApp('chat:private', 'installed', leavePrivate)} />
