import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { taskFolder, tasksPlaceRules } from '../../../source/presentation/tasks/Previews/TasksWorlds'
import type { TasksWorld } from '../../../source/presentation/tasks/Previews/TasksWorlds'
import { TasksViewModel } from '../../../source/presentation/tasks/ViewModels/TasksViewModel'

/** Lets what the stand-in answered land: each awaited read takes a turn. */
export async function settle(): Promise<void> {
  for (let round = 0; round < 10; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The Tasks place over the stand-in (design 1p): a world, a still clock the test steps, the toast, the sheet and the alert it speaks through, recorded. */
export function tasksPlace(options: { readonly world?: TasksWorld; readonly apogee?: ApogeeMode; readonly rules?: Rules } = {}) {
  const child = new StandInChild({ rules: options.rules ?? tasksPlaceRules(options.world ?? 'ledger'), apogee: options.apogee ?? 'installed' }, modeRules)
  const clock = new Clock({ still: true })
  const registry = new ChildRegistry()
  const tasks = new TasksViewModel(new ReadRunner(child), new JobStarter(child, clock, registry), () => clock.now)
  tasks.folder = taskFolder
  const said: string[] = []
  const presented: string[] = []
  const asked: string[] = []
  let dismissed = 0
  tasks.say = (text) => said.push(text)
  tasks.present = (kind) => presented.push(kind)
  tasks.dismissSheet = () => (dismissed += 1)
  tasks.ask = (kind) => asked.push(kind)
  tasks.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  /** `count` ticks of the clock, what they played landed. */
  const tick = async (count = 1) => {
    clock.advance(count)
    await settle()
  }
  return { tasks, child, clock, registry, said, presented, asked, dismissed: () => dismissed, tick }
}

/** The calls the stand-in answered since `from`, as `<kind> <arguments>`. */
export function callsSince(child: StandInChild, from = 0): string[] {
  return child.calls.slice(from).map((call) => `${call.kind} ${call.arguments.join(' ')}`)
}
