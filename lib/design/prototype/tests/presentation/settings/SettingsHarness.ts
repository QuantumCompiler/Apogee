import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { ClockSettings } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { settingsWorld } from '../../../source/presentation/settings/Previews/SettingsWorlds'
import type { SettingsWorld } from '../../../source/presentation/settings/Previews/SettingsWorlds'
import { libraryRules } from '../../../source/presentation/sessions/Previews/LibraryRules'
import { modelsPlaceRules } from '../../../source/presentation/models/Previews/ModelsWorlds'

/** Lets every read in flight land. */
export const settled = async (rounds = 6) => {
  for (let round = 0; round < rounds; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/**
 * The stand-in over a Settings world (design 1i), as a test builds it: the
 * world's answers, the Models place's (the roles' verbs and `models status`)
 * and the library's (`chats recall` and its forget) beside them, as every
 * preset carries them.
 */
export function standIn(world: SettingsWorld | Rules = 'default', clock: ClockSettings = { still: true }, apogee: ApogeeMode = 'installed') {
  const own = typeof world === 'string' ? settingsWorld(world) : world
  const models = modelsPlaceRules()
  const library = libraryRules()
  const rules: Rules = {
    reads: [...(models.reads ?? []), ...(own.reads ?? []), ...library.reads],
    commands: [...(models.commands ?? []), ...(own.commands ?? []), ...library.commands],
    jobs: [...(own.jobs ?? [])],
  }
  const child = new StandInChild({ rules, apogee }, { configTypoRefusal: agentsListBadConfig })
  const ticks = new Clock(clock)
  const registry = new ChildRegistry()
  return { child, clock: ticks, reads: new ReadRunner(child), jobs: new JobStarter(child, ticks, registry), registry, binary: StandInChild.binary }
}
