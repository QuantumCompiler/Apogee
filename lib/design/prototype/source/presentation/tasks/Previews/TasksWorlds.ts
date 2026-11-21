import taskListAll from '@recorded/presentation/tasks/Fixtures/task-list-all.json?raw'
import taskListNewest50 from '@recorded/presentation/tasks/Fixtures/task-list-newest-50.json?raw'
import taskStatus from '@recorded/presentation/tasks/Fixtures/task-status.json?raw'
import taskStatusUses from '@recorded/presentation/tasks/Fixtures/task-status-uses.json?raw'
import taskStatusStalled from '@recorded/presentation/tasks/Fixtures/task-status-stalled.json?raw'
import taskStatusStalledGranted from '@recorded/presentation/tasks/Fixtures/task-status-stalled-granted.json?raw'
import taskStatusHaltedDone from '@recorded/presentation/tasks/Fixtures/task-status-halted-done.json?raw'
import taskStatusExhausted from '@recorded/presentation/tasks/Fixtures/task-status-exhausted.json?raw'
import taskStatusInterrupted from '@recorded/presentation/tasks/Fixtures/task-status-interrupted.json?raw'
import taskRun from '@recorded/presentation/tasks/Fixtures/task-run.jsonl?raw'
import taskRunUses from '@recorded/presentation/tasks/Fixtures/task-run-uses.jsonl?raw'
import taskResume from '@recorded/presentation/tasks/Fixtures/task-resume.jsonl?raw'
import taskRunRefused from '@recorded/presentation/tasks/Fixtures/task-run-refused.err?raw'
import taskResumeRefused from '@recorded/presentation/tasks/Fixtures/task-resume-refused.err?raw'
import taskRecipes from '@recorded/business/reads/Fixtures/task-recipes.json?raw'
import taskRecipeShow from '@recorded/business/reads/Fixtures/task-recipe-show.json?raw'
import suitesModelsStatus from '@recorded/business/reads/Fixtures/suites-models-status.json?raw'
import suitesConfigGet from '@recorded/business/reads/Fixtures/suites-config-get.txt?raw'
import taskListHandmade from '../../../../fixtures/presentation/tasks/task-list-handmade.json?raw'
import taskListStalledHandmade from '../../../../fixtures/presentation/tasks/task-list-stalled-handmade.json?raw'
import taskListStalledGrantedHandmade from '../../../../fixtures/presentation/tasks/task-list-stalled-granted-handmade.json?raw'
import taskRunHaltAfterPlanHandmade from '../../../../fixtures/presentation/tasks/task-run-halt-after-plan-handmade.jsonl?raw'
import taskRunHaltAfterRound1Handmade from '../../../../fixtures/presentation/tasks/task-run-halt-after-round-1-handmade.jsonl?raw'
import taskRunCancelPlanHandmade from '../../../../fixtures/presentation/tasks/task-run-cancel-plan-handmade.jsonl?raw'
import taskRunCancelRound1Handmade from '../../../../fixtures/presentation/tasks/task-run-cancel-round-1-handmade.jsonl?raw'
import taskRunCancelRound2Handmade from '../../../../fixtures/presentation/tasks/task-run-cancel-round-2-handmade.jsonl?raw'
import taskResumeHaltStartingHandmade from '../../../../fixtures/presentation/tasks/task-resume-halt-starting-handmade.jsonl?raw'
import taskResumeCancelStartingHandmade from '../../../../fixtures/presentation/tasks/task-resume-cancel-starting-handmade.jsonl?raw'
import taskResumeCancelRound3Handmade from '../../../../fixtures/presentation/tasks/task-resume-cancel-round-3-handmade.jsonl?raw'
import taskHalt011456Handmade from '../../../../fixtures/presentation/tasks/task-halt-011456-handmade.txt?raw'
import taskCancel011456Handmade from '../../../../fixtures/presentation/tasks/task-cancel-011456-handmade.txt?raw'
import taskHalt021224Handmade from '../../../../fixtures/presentation/tasks/task-halt-021224-handmade.txt?raw'
import taskCancel021224Handmade from '../../../../fixtures/presentation/tasks/task-cancel-021224-handmade.txt?raw'
import taskRecipeShowCaptureDecisionsHandmade from '../../../../fixtures/business/reads/task-recipe-show-capture-decisions-handmade.json?raw'
import taskRecipeShowChangelogDraftHandmade from '../../../../fixtures/business/reads/task-recipe-show-changelog-draft-handmade.json?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import { tasksRules } from './TasksWorldRules'
import type { Lookup, TasksWorld } from './TasksWorldRules'

export * from './TasksWorldRules'

/**
 * The Tasks place's recordings (design 1p), each a `?raw` import the build
 * inlines, named in `recordings` by its file name; the answers built from
 * them are `TasksWorldRules.ts`, which imports none. The recorded ones are
 * the Mac app's (`task-*` under `presentation/tasks/Fixtures/`, the recipes
 * and suites under `business/reads/Fixtures/`); the hand-made ones, each in
 * the CLI's shape from a recording, are the three ledgers, the live run's and
 * the resume's endings when a halt or a cancel stops them, the two stops'
 * lines, and the two recipes no show recorded:
 * - `task-list-handmade.json`, `task-list-stalled-handmade.json`,
 *   `task-list-stalled-granted-handmade.json`: `task list`'s shape
 *   (`task-list-killed.json`), each row a recorded `task status`'s own, newest
 *   first;
 * - `task-run-halt-after-{plan,round-1}-handmade.jsonl` and
 *   `task-run-cancel-{plan,round-1,round-2}-handmade.jsonl`: the closing
 *   lines of `task-run.jsonl`'s own run stopped there -- its `task_finished`
 *   halted or cancelled in `runner.cpp`'s words, after the machine reporter's
 *   `cancelled` error and the round's interrupted end for a cancel;
 * - `task-resume-{halt,cancel}-starting-handmade.jsonl` and
 *   `task-resume-cancel-round-3-handmade.jsonl`: the same for
 *   `task-resume.jsonl`'s run, a stop before round 3 the CLI's 57u;
 * - `task-{halt,cancel}-{011456,021224}-handmade.txt`: `task_cmd.cpp`'s line
 *   for a stop asked of a running task;
 * - `task-recipe-show-{capture-decisions,changelog-draft}-handmade.json`:
 *   `task-recipes.json`'s rows with show's keys, their goals the old
 *   prototype's `recipes()` words.
 */
const recordings: Readonly<Record<string, string>> = {
  'task-list-all.json': taskListAll,
  'task-list-newest-50.json': taskListNewest50,
  'task-status.json': taskStatus,
  'task-status-uses.json': taskStatusUses,
  'task-status-stalled.json': taskStatusStalled,
  'task-status-stalled-granted.json': taskStatusStalledGranted,
  'task-status-halted-done.json': taskStatusHaltedDone,
  'task-status-exhausted.json': taskStatusExhausted,
  'task-status-interrupted.json': taskStatusInterrupted,
  'task-run.jsonl': taskRun,
  'task-run-uses.jsonl': taskRunUses,
  'task-resume.jsonl': taskResume,
  'task-run-refused.err': taskRunRefused,
  'task-resume-refused.err': taskResumeRefused,
  'task-recipes.json': taskRecipes,
  'task-recipe-show.json': taskRecipeShow,
  'suites-models-status.json': suitesModelsStatus,
  'suites-config-get.txt': suitesConfigGet,
  'task-list-handmade.json': taskListHandmade,
  'task-list-stalled-handmade.json': taskListStalledHandmade,
  'task-list-stalled-granted-handmade.json': taskListStalledGrantedHandmade,
  'task-run-halt-after-plan-handmade.jsonl': taskRunHaltAfterPlanHandmade,
  'task-run-halt-after-round-1-handmade.jsonl': taskRunHaltAfterRound1Handmade,
  'task-run-cancel-plan-handmade.jsonl': taskRunCancelPlanHandmade,
  'task-run-cancel-round-1-handmade.jsonl': taskRunCancelRound1Handmade,
  'task-run-cancel-round-2-handmade.jsonl': taskRunCancelRound2Handmade,
  'task-resume-halt-starting-handmade.jsonl': taskResumeHaltStartingHandmade,
  'task-resume-cancel-starting-handmade.jsonl': taskResumeCancelStartingHandmade,
  'task-resume-cancel-round-3-handmade.jsonl': taskResumeCancelRound3Handmade,
  'task-halt-011456-handmade.txt': taskHalt011456Handmade,
  'task-cancel-011456-handmade.txt': taskCancel011456Handmade,
  'task-halt-021224-handmade.txt': taskHalt021224Handmade,
  'task-cancel-021224-handmade.txt': taskCancel021224Handmade,
  'task-recipe-show-capture-decisions-handmade.json': taskRecipeShowCaptureDecisionsHandmade,
  'task-recipe-show-changelog-draft-handmade.json': taskRecipeShowChangelogDraftHandmade,
}

/** A file's text, by its name; a name no import holds is refused, naming it. */
export function recording(name: string): string {
  const text = recordings[name]
  if (text === undefined) throw new Error(`no fixture named ${name}`)
  return text
}

/** What Tasks reads and runs over `world` (the ledger unless named): every preset's, since ⌘4 opens it from every screen. */
export function tasksPlaceRules(world: TasksWorld = 'ledger', look: Lookup = recording): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  return tasksRules(world, look)
}

/** `rules` with `world`'s Tasks answers after them, so they are the ones given: a Tasks preset's own world over every preset's ledger. */
export function withTasksWorld(rules: Rules, world: TasksWorld): Rules {
  const own = tasksRules(world, recording)
  return { ...rules, reads: [...(rules.reads ?? []), ...own.reads], commands: [...(rules.commands ?? []), ...own.commands], jobs: [...(rules.jobs ?? []), ...own.jobs] }
}
