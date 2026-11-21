import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { graphPlaceRules, graphWorld } from '../../../source/presentation/graph/Previews/GraphWorlds'
import { GraphViewModel } from '../../../source/presentation/graph/ViewModels/GraphViewModel'

/** Lets what the stand-in answered land: each awaited read, and each replayed job's `for await`, takes a turn. */
export async function settle(): Promise<void> {
  for (let round = 0; round < 6; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The Code graph place over the stand-in (design 1o): the place's world, a still clock tests advance by hand, and the doors it speaks through, recorded. */
export function graphPlace(options: { readonly apogee?: ApogeeMode; readonly rules?: Rules } = {}) {
  const child = new StandInChild({ rules: options.rules ?? graphPlaceRules(), apogee: options.apogee ?? 'installed' }, modeRules)
  const clock = new Clock({ still: true })
  const graph = new GraphViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()))
  graph.useWorld(graphWorld)
  const said: string[] = []
  const sheets: string[] = []
  const asked: string[] = []
  const menus: Array<string | undefined> = []
  const askedAbout: Array<readonly [string, string]> = []
  graph.say = (text) => said.push(text)
  graph.present = (kind) => sheets.push(kind)
  graph.dismissSheet = () => sheets.push('dismissed')
  graph.ask = (kind) => asked.push(kind)
  graph.menuChanged = (owner) => menus.push(owner)
  graph.askAbout = (file, draft) => askedAbout.push([file, draft])
  graph.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  return { graph, child, clock, said, sheets, asked, menus, askedAbout }
}

/** The calls the stand-in answered since `from`, as `<kind> <arguments>`. */
export function callsSince(child: StandInChild, from: number): string[] {
  return child.calls.slice(from).map((call) => `${call.kind} ${call.arguments.join(' ')}`)
}

/** Plays `ticks` of the still clock, letting each land. */
export async function play(clock: Clock, ticks: number): Promise<void> {
  for (let tick = 0; tick < ticks; tick += 1) {
    clock.advance(1)
    await settle()
  }
}
