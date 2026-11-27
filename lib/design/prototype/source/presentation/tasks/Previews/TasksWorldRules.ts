import type { AskedStop, Rule, Rules } from '../../../infrastructure/process/StandInChild'

/**
 * The Tasks place's stand-in answers (design 1p), every one a recording of
 * the Mac app's or a `*-handmade*` file in the CLI's shape: the ledger
 * (`task list`), each task's page (`task status <id>`), the recorded runs
 * (`task-run.jsonl`, `task-run-uses.jsonl`) and resume (`task-resume.jsonl`)
 * with their stops (`task halt`, `task cancel`), the two refusals, the
 * recipes, and the suites the sheet offers.
 *
 * Each file is named by its file name, looked up in `TasksWorlds.ts`'
 * `recordings`, where the `?raw` imports are: this module imports none, so
 * the fixture check lists what every preset names without loading a fixture.
 */

/** Where a world's recordings come from: `TasksWorlds.ts`' `recording`, or a test's lookup that lists the names. */
export type Lookup = (name: string) => string

/** The recorded tasks, by what each is to the place. */
export const tasks = {
  /** `task-run.jsonl`'s: say hello, run on the mock backend, its two rounds spent. */
  live: 'task-20261021-011456',
  /** `task-run-uses.jsonl`'s: Write the report, a call let through on its grant and one refused. */
  report: 'task-20261109-021211',
  /** Find the word: stalled, then resumed to done (`task-resume.jsonl`). */
  stalled: 'task-20261109-021224',
  /** Find the word, writing notes: stalled, granted write_file, its resume then refused by the config. */
  stalledGranted: 'task-20261109-021236',
  /** Answer when asked: halted after a round, then finished done. */
  done: 'task-20261109-021308',
  /** say hello: its one round spent. */
  exhausted: 'task-20261026-060918',
  /** say hello: killed mid-round. */
  interrupted: 'task-20261026-060911',
} as const

/** The folder every recorded task ran in, and the sheet's (read-only, as the old one is). */
export const taskFolder = '/Users/someone/work'

/** `task-run.jsonl`'s run, as it was typed for the recording: `task run "say hello" --require mock --rounds 2 --output-format stream-json`. */
export const recordedRun: readonly string[] = ['task', 'run', 'say hello', '--require', 'mock', '--rounds', '2', '--output-format', 'stream-json']

/**
 * `task-run-uses.jsonl`'s run, as its recorded `task status` states it -- the
 * goal, its file check, tools on, `run_command` granted, one round -- the
 * recording naming no line of its own.
 */
export const reportRun: readonly string[] = ['task', 'run', 'Write the report', '--require-file', '/Users/someone/work/report.txt', '--tools', '--allow', 'run_command', '--rounds', '1', '--output-format', 'stream-json']

/** The old `newtask` frame's goal: the sheet filled, both grants given. */
export const newTaskGoal = 'Make tests/test_parsers.py pass on Python 3.12'

/** The sheet as the `newtask` frame fills it, started: the line the recorded home's config refuses, asking `--allow write_file` (`task-run-refused.err`). */
export const refusedStart = (suite: string): readonly string[] => ['task', 'run', newTaskGoal, '--tools', '--allow', 'write_file', '--allow', 'run_command', '--suite', suite, '--output-format', 'stream-json']

/** The recipe the `newtask:recipe` frame picks, its input and the grant it ticks. */
export const recipeFrame = { recipe: 'digest-folder', input: 'docs', grants: ['write_file'] } as const

/**
 * A Tasks world: the ledger a preset reads. Each ledger is hand-made in
 * `task list`'s shape, every row a recorded task status's own: `ledger` the
 * old frame's done, exhausted and interrupted tasks; `resuming` with the
 * stalled task whose resume is recorded; `resumerefused` with the stalled
 * task whose resume the config refused; `sweep` the recorded ledger of 60,
 * the newest 50 listed.
 */
export type TasksWorld = 'ledger' | 'resuming' | 'resumerefused' | 'sweep'

/** The Tasks presets, in the old `data-props` order, and each one's world: what the fixture check walks. */
export const tasksPresets: Readonly<Record<string, TasksWorld>> = {
  tasks: 'ledger',
  'tasks:denied': 'ledger',
  'tasks:more': 'sweep',
  'tasks:resuming': 'resuming',
  'tasks:refused': 'ledger',
  'tasks:resumerefused': 'resumerefused',
  newtask: 'ledger',
  'newtask:recipe': 'ledger',
}

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })
const exact = (args: readonly string[], stdout: string): Rule => ({ arguments: args, exact: true, answer: { stdout } })

/** Every task's page the place may read: each recorded `task status`. */
const statuses: ReadonlyArray<readonly [string, string]> = [
  [tasks.live, 'task-status.json'],
  [tasks.report, 'task-status-uses.json'],
  [tasks.stalled, 'task-status-stalled.json'],
  [tasks.stalledGranted, 'task-status-stalled-granted.json'],
  [tasks.done, 'task-status-halted-done.json'],
  [tasks.exhausted, 'task-status-exhausted.json'],
  [tasks.interrupted, 'task-status-interrupted.json'],
]

/**
 * The live run asked to stop (`task-run.jsonl`, 30 lines: the plan through
 * its 11th, round 1 from the 12th to its end at the 20th, round 2 to the
 * 29th): a halt lands as the turn in flight ends -- after the plan, or after
 * round 1; in round 2, the last, its budget decides first and it ends
 * exhausted as recorded -- and a cancel ends the turn in flight now. Between
 * a turn's end line and the next round's start, either counts as the next
 * round's, as the CLI reads one: `runner.cpp`'s `stop_if_asked` runs once,
 * right after the end line, and the next round starts unchecked.
 */
function liveStops(look: Lookup): AskedStop[] {
  return [
    {
      by: ['task', 'halt', tasks.live],
      endings: [
        { before: 11, through: 11, stdout: look('task-run-halt-after-plan-handmade.jsonl'), exit: 1 },
        { before: 20, through: 20, stdout: look('task-run-halt-after-round-1-handmade.jsonl'), exit: 1 },
      ],
    },
    {
      by: ['task', 'cancel', tasks.live],
      endings: [
        { before: 11, stdout: look('task-run-cancel-plan-handmade.jsonl'), exit: 130 },
        { before: 20, stdout: look('task-run-cancel-round-1-handmade.jsonl'), exit: 130 },
        { before: 29, stdout: look('task-run-cancel-round-2-handmade.jsonl'), exit: 130 },
      ],
    },
  ]
}

/**
 * The resume asked to stop (`task-resume.jsonl`, 15 lines: its session, its
 * `task_started`, then round 3 from the 3rd to its end at the 14th): a stop
 * sent while it starts up, before its `task_started`, is honoured as it
 * begins (the CLI's 57u: `stop_before_start`, right after that line), before
 * round 3; one sent once that line is out counts as round 3's -- a cancel
 * ends round 3 now, and a halt lets it end, done, as recorded.
 */
function resumeStops(look: Lookup): AskedStop[] {
  return [
    { by: ['task', 'halt', tasks.stalled], endings: [{ before: 2, through: 2, stdout: look('task-resume-halt-starting-handmade.jsonl'), exit: 1 }] },
    {
      by: ['task', 'cancel', tasks.stalled],
      endings: [
        { before: 2, through: 2, stdout: look('task-resume-cancel-starting-handmade.jsonl'), exit: 130 },
        { before: 14, stdout: look('task-resume-cancel-round-3-handmade.jsonl'), exit: 130 },
      ],
    },
  ]
}

/** The ticks a resume takes to start up -- its session line, then held -- before its `task_started`: the old prototype's 4. */
export const resumeStartTicks = 4

/** What the Tasks place reads and runs over `world`. */
export function tasksRules(world: TasksWorld, look: Lookup): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  const ledger = { ledger: 'task-list-handmade.json', resuming: 'task-list-stalled-handmade.json', resumerefused: 'task-list-stalled-granted-handmade.json', sweep: 'task-list-newest-50.json' }[world]
  const all = world === 'sweep' ? 'task-list-all.json' : ledger
  return {
    reads: [
      exact(['task', 'list'], look(ledger)),
      exact(['task', 'list', '--all'], look(all)),
      ...statuses.map(([id, file]) => exact(['task', 'status', id], look(file))),
      exact(['task', 'recipes'], look('task-recipes.json')),
      exact(['task', 'recipes', 'show', 'digest-folder'], look('task-recipe-show.json')),
      exact(['task', 'recipes', 'show', 'capture-decisions'], look('task-recipe-show-capture-decisions-handmade.json')),
      exact(['task', 'recipes', 'show', 'changelog-draft'], look('task-recipe-show-changelog-draft-handmade.json')),
      // The sheet's suites, as the Suites place reads them.
      read(['models', 'status'], look('suites-models-status.json')),
    ],
    commands: [
      exact(['config', 'get', 'suites'], look('suites-config-get.txt')),
      exact(['task', 'halt', tasks.live], look('task-halt-011456-handmade.txt')),
      exact(['task', 'cancel', tasks.live], look('task-cancel-011456-handmade.txt')),
      exact(['task', 'halt', tasks.stalled], look('task-halt-021224-handmade.txt')),
      exact(['task', 'cancel', tasks.stalled], look('task-cancel-021224-handmade.txt')),
    ],
    jobs: [
      // A run of a goal the CLI recorded running replays that recording: say hello's, Write the report's.
      { arguments: ['task', 'run', 'say hello'], answer: { stdout: look('task-run.jsonl'), exit: 1, asked: liveStops(look) } },
      { arguments: ['task', 'run', 'Write the report'], answer: { stdout: look('task-run-uses.jsonl'), exit: 1 } },
      // The recorded home's config denies write_file: the sheet's start that asks it is refused before it begins, on every suite.
      ...['cloud', 'fast', 'pair', 'shared', 'vast'].map((suite): Rule => ({ arguments: refusedStart(suite), exact: true, answer: { stderr: look('task-run-refused.err'), exit: 1 } })),
      { arguments: ['task', 'resume', tasks.stalled], answer: { stdout: look('task-resume.jsonl'), holdAt: 1, exitAfter: resumeStartTicks - 1, asked: resumeStops(look) } },
      { arguments: ['task', 'resume', tasks.stalledGranted], answer: { stderr: look('task-resume-refused.err'), exit: 1 } },
    ],
  }
}

/** The file names `world`'s answers look up, each once: what the fixture check holds to exist. */
export function tasksPlaceNames(world: TasksWorld): string[] {
  const named = new Set<string>()
  tasksRules(world, (name) => (named.add(name), ''))
  return [...named]
}
