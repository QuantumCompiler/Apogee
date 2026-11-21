import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { ClockSettings } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { trainingWorld } from '../../../source/presentation/training/Previews/TrainingWorlds'
import type { TrainingWorld } from '../../../source/presentation/training/Previews/TrainingWorlds'
import { TrainingViewModel } from '../../../source/presentation/training/ViewModels/TrainingViewModel'

/** Lets every read in flight land. */
export const settled = async (rounds = 6) => {
  for (let round = 0; round < rounds; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The place over a Training world (design 1j), as a test builds it: connected and read, its toasts and its asks kept. */
export async function trainingOver(world: TrainingWorld | Rules, clock: ClockSettings = {}, apogee: ApogeeMode = 'installed') {
  const rules = typeof world === 'string' ? trainingWorld(world) : world
  const child = new StandInChild({ rules, apogee }, { configTypoRefusal: agentsListBadConfig })
  const ticks = new Clock(clock)
  const registry = new ChildRegistry()
  const training = new TrainingViewModel(new ReadRunner(child), new JobStarter(child, ticks, registry))
  const said: string[] = []
  const asked: string[] = []
  const sheets: string[] = []
  training.say = (text) => said.push(text)
  training.ask = (kind) => asked.push(kind)
  training.present = (kind) => sheets.push(kind)
  training.dismissSheet = () => sheets.push('dismissed')
  training.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  await training.appeared()
  /** `n` ticks, each one's reads landed. */
  const advance = async (n: number) => {
    for (let tick = 0; tick < n; tick += 1) {
      ticks.advance(1)
      await settled(2)
    }
    await settled()
  }
  return { training, child, clock: ticks, registry, said, asked, sheets, advance }
}
