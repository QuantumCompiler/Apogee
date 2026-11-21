import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { ClockSettings } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { modelsWorld } from '../../../source/presentation/models/Previews/ModelsWorlds'
import type { ModelsWorld } from '../../../source/presentation/models/Previews/ModelsWorlds'

/** Lets every read in flight land. */
export const settled = async (rounds = 4) => {
  for (let round = 0; round < rounds; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The stand-in over a Models world (design 1h), as a test builds it: the child, the clock, the reads, the jobs and the registry. */
export function standIn(world: ModelsWorld | Rules, clock: ClockSettings = {}, apogee: ApogeeMode = 'installed') {
  const rules = typeof world === 'string' ? modelsWorld(world) : world
  const child = new StandInChild({ rules, apogee }, { configTypoRefusal: agentsListBadConfig })
  const ticks = new Clock(clock)
  const registry = new ChildRegistry()
  return { child, clock: ticks, reads: new ReadRunner(child), jobs: new JobStarter(child, ticks, registry), registry, binary: StandInChild.binary }
}
