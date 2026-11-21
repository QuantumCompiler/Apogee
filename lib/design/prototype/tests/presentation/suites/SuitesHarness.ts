import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { suitesPlaceRules } from '../../../source/presentation/suites/Previews/SuitesWorlds'
import { SuitesViewModel } from '../../../source/presentation/suites/ViewModels/SuitesViewModel'

/** Lets what the stand-in answered land: each awaited read takes a turn. */
export async function settle(): Promise<void> {
  for (let round = 0; round < 8; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The Suites place over the stand-in (design 1p): the place's recordings, its toast and its two doors recorded. */
export function suitesPlace(options: { readonly apogee?: ApogeeMode; readonly rules?: Rules; readonly switchable?: boolean } = {}) {
  const child = new StandInChild({ rules: options.rules ?? suitesPlaceRules(), apogee: options.apogee ?? 'installed' }, modeRules)
  const suites = new SuitesViewModel(new ReadRunner(child))
  const said: string[] = []
  const opened: Array<readonly [string, boolean]> = []
  const switched: Array<readonly [string, boolean]> = []
  let defaults = 0
  suites.say = (text) => said.push(text)
  suites.openChat = (suite, force) => opened.push([suite, force])
  suites.switchSession = (suite, force) => switched.push([suite, force])
  suites.sessionSwitchable = () => options.switchable ?? true
  suites.defaultChanged = () => (defaults += 1)
  suites.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  return { suites, child, said, opened, switched, defaults: () => defaults }
}
