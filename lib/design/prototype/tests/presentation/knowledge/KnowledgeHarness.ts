import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { collectionQuestion, knowledgePlaceRules, recordedSearches, recordsQuestion } from '../../../source/presentation/knowledge/Previews/KnowledgeWorlds'
import { CollectionsViewModel } from '../../../source/presentation/knowledge/ViewModels/CollectionsViewModel'
import { RecordsViewModel } from '../../../source/presentation/knowledge/ViewModels/RecordsViewModel'

/** Lets what the stand-in answered land: each awaited read takes a turn. */
export async function settle(): Promise<void> {
  for (let round = 0; round < 6; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The Knowledge place over the stand-in (design 1o): the place's world, the toast it speaks through, recorded, and the clock its runs play on (GUI M22), driven by hand. */
export function knowledgePlace(options: { readonly apogee?: ApogeeMode; readonly rules?: Rules } = {}) {
  const child = new StandInChild({ rules: options.rules ?? knowledgePlaceRules(), apogee: options.apogee ?? 'installed' }, modeRules)
  const reads = new ReadRunner(child)
  const clock = new Clock()
  const jobs = new JobStarter(child, clock)
  const records = new RecordsViewModel(reads, jobs)
  const collections = new CollectionsViewModel(reads, jobs)
  collections.recordedSearches = recordedSearches
  // What an empty field asks, as the composition hands them in: the old kno.ask's and kno.search's questions.
  records.emptyQuestion = recordsQuestion
  collections.emptyQuestions = { notes: collectionQuestion }
  const said: string[] = []
  records.say = (text) => said.push(text)
  collections.say = (text) => said.push(text)
  const binary = child.locateBinary().case === 'found' ? StandInChild.binary : undefined
  records.connect(binary)
  collections.connect(binary)
  return { records, collections, child, said, clock }
}

/** The calls the stand-in answered since `from`, as `<kind> <arguments>`. */
export function callsSince(child: StandInChild, from: number): string[] {
  return child.calls.slice(from).map((call) => `${call.kind} ${call.arguments.join(' ')}`)
}
