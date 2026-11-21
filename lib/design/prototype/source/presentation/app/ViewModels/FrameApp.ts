import type { Timers } from '../../../infrastructure/clock/Clock'
import type { BoardMount } from '../../gallery/ViewModels/GalleryViewModel'
import { sessionSamples } from '../../session/Previews/SessionPresets'
import { modeRules } from '../Previews/ModeRules'
import { presetFor } from '../Previews/Presets'
import { AppViewModel } from './AppViewModel'

/**
 * The app a gallery frame mounts (design 1q): a composition of its own over
 * the board's preset -- the screen's start, still, or the App board's,
 * live at the default start -- in the board's theme and `apogee` mode. Each
 * frame its own stand-in, clock and view-models, as each canvas board was
 * its own `main.html`.
 */
export function frameApp(mount: BoardMount, timers?: Timers): AppViewModel {
  const { preset } = presetFor({ ...(mount.start === undefined ? {} : { start: mount.start }), still: mount.still, apogee: mount.apogee })
  const app = new AppViewModel(preset, modeRules, timers, sessionSamples)
  app.theme.update({ themeChoice: mount.theme })
  return app
}
