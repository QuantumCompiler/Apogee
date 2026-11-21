import type { Answer, Rule, Rules } from '../../../infrastructure/process/StandInChild'

/**
 * The Training place's stand-in answers (design 1j), every one a recording of
 * the Mac app's or a `*-handmade*` file in the CLI's shape, one world a
 * state of a scratch home the CLI recorded:
 *
 * - `recorded`: 4i's and 4j's home before any pipeline -- one run that failed
 *   its gate (`train-status.json`), the three promoted ledgers, the regimes,
 *   the kits, one dataset, no named pipeline and no cycle yet (`training`,
 *   `training:datasets`, `training:regimes`, and the place on every other
 *   preset);
 * - `pipelines`: the same with `nightly` and `skills` named -- the `skills`
 *   run aborting at its second stage and its resume, the cycle's three
 *   recorded passes in their order (`training:pipelines`);
 * - `cycle`: that home after the cycle's skipped pass and its passing one,
 *   four pipeline runs (`train-pipeline-status-list.json`), the next pass the
 *   one that fails and halts it (`training:cycle`);
 * - `stalelock`, `unreadable`, `resumeheld`, `passrunning`: GUI 6i's home
 *   over the CLI's 57g and 57h (2026-11-09) -- a dead pass's lock, an
 *   unreadable history, a run a live process holds, and the cycle pass as it
 *   stood mid-pass (`training:stalelock` and `training:cyclepass`,
 *   `training:unreadable`, `training:resumeheld`, `training:passrunning`).
 *
 * Each file is named by its file name, looked up in `TrainingWorlds.ts`'s
 * `recordings`, where the `?raw` imports are: this module imports none, so
 * the fixture check (`TrainingFixtures.test.ts`) lists what every preset
 * names without loading a fixture, and a file missing on disk fails each
 * preset that names it, naming the preset.
 */
export type TrainingWorld = 'recorded' | 'pipelines' | 'cycle' | 'stalelock' | 'unreadable' | 'resumeheld' | 'passrunning'

export const trainingWorlds: readonly TrainingWorld[] = ['recorded', 'pipelines', 'cycle', 'stalelock', 'unreadable', 'resumeheld', 'passrunning']

/** Where a world's recordings come from: `TrainingWorlds.ts`'s `recording`, or a test's lookup that lists the names. */
export type Lookup = (name: string) => string

/**
 * The run `train-run.err` recorded, 20261021-051826's base and data: what
 * New run… trains again on the `recorded` and `pipelines` worlds, whose
 * `train status` lists it alone. On the other worlds, the run shown is one no
 * recording trained again, so New run… says the stand-in's words.
 */
export const recordedRun = { id: '20261021-051826', student: '/Users/someone/work/student', dataset: '/Users/someone/work/data.jsonl' } as const

/** The dataset the pull sheet's recording pulled, and the folder it landed in. */
export const pulledDataset = { ref: 'someone/heat-notes', path: '/Users/someone/.apogee/training/datasets/raw/someone--heat-notes' } as const

/** The backend name the merged promote's hand-made output registers. */
export const promotedBackend = 'student-tuned'

/** GUI 6i's ids: the cycle pass, the run a live process holds, and that process. */
export const sweep = { cyclePass: 'cycle-20261109-041521', keptFailed: 'pipe-20261109-041458', specFileGone: 'pipe-20261109-041444', held: 'pipe-20261109-040503', holder: 67959 } as const

const stream = ['--output-format', 'stream-json']

function rules(look: Lookup) {
  const read = (args: readonly string[], name: string): Rule => ({ arguments: args, answer: { stdout: look(name) } })
  const refused = (args: readonly string[], name: string, exact = false): Rule => ({ arguments: args, ...(exact ? { exact } : {}), answer: { stderr: look(name), exit: 1 } })
  const job = (args: readonly string[], answer: Answer | readonly [Answer, ...Answer[]], exact = false): Rule => ({ arguments: args, ...(exact ? { exact } : {}), answer })
  return { read, refused, job }
}

/** What every world answers: the ledgers, the regimes, the kits, the datasets, the pull and the prepare; New run… and the promotes of `recordedRun` (and the passed runs' recorded adapter promotes). */
function shared(look: Lookup): Required<Rules> {
  const { read, refused, job } = rules(look)
  const promote = (run: string, ...rest: string[]) => ['train', 'promote', run, '--as', ...rest]
  return {
    reads: [read(['train', 'versions'], 'train-versions-adapters.json'), read(['train', 'regime', 'list'], 'train-regimes.json'), read(['datasets', 'kits'], 'datasets-kits.json'), read(['datasets', 'list'], 'datasets-list.json')],
    commands: [{ arguments: ['train', 'cycle', 'halt'], answer: { stdout: look('train-cycle-halt.txt') } }],
    jobs: [
      // `train run` speaks on stderr alone: its progress is the busy line (the stand-in's `busy` rule).
      job(['train', 'run', recordedRun.student, '--dataset', recordedRun.dataset], { stderr: look('train-run.err'), busy: true }, true),
      // The failed run's promote, unforced, is refused at its run's eval gate (promote.cpp:75 and :45) -- merged, or as an adapter
      // onto an entry whose base is not quantized (train.cpp's precheck); q4's quantized base would be gated where it serves.
      ...['based', 'q4', 'tuned', promotedBackend].map((backend) => refused(promote(recordedRun.id, backend), 'train-promote-gate-failed-handmade.err', true)),
      ...['based', 'tuned'].map((backend) => refused(promote(recordedRun.id, backend, '--adapter'), 'train-promote-gate-failed-handmade.err', true)),
      // Forced past its gate, each ledger refuses the other kind in its own words (53b), as recorded for these two entries.
      refused(promote(recordedRun.id, 'based', '--force'), 'train-promote-names-adapter.err', true),
      refused(promote(recordedRun.id, 'tuned', '--adapter', '--force'), 'train-promote-holds-merged.err', true),
      job(promote(recordedRun.id, promotedBackend, '--force'), { stdout: look('train-promote-merged-handmade.out'), stderr: look('train-promote-merged-handmade.err'), busy: true }, true),
      // The passed runs' adapter promotes, as 53b and 53c recorded them.
      job(promote('20261026-065637', 'based', '--adapter'), { stdout: look('train-promote-adapter.out'), stderr: look('train-promote-adapter.err'), busy: true }, true),
      refused(promote('20261026-065638', 'q4', '--adapter'), 'train-promote-adapter-no-suite.err', true),
      // The pull: streamed (the CLI's M30), or as text on a binary that does not stream it.
      job(['datasets', 'pull', pulledDataset.ref, ...stream], { stdout: look('datasets-pull-handmade.jsonl') }, true),
      job(['datasets', 'pull', pulledDataset.ref], { stdout: look('datasets-pull-handmade.txt') }, true),
      job(['datasets', 'prepare', pulledDataset.path], { stderr: look('datasets-prepare-heat-notes-handmade.err'), exit: 1, busy: true }, true),
    ],
    chats: [],
  }
}

/** One world's own: its `train status`, its pipelines, its cycle and what its acts replay. */
function own(world: TrainingWorld, look: Lookup): Required<Rules> {
  const { read, refused, job } = rules(look)
  const status = (id: string, name: string) => read(['train', 'pipeline', 'status', id], name)
  const sweepReads = (held: boolean, running: boolean): Rule[] => [
    read(['train', 'pipeline', 'list'], 'train-pipeline-list-sweep.json'),
    // Mid-pass, the list says the pass running too, as its manifest does (both hand-made from their recordings, as the Mac's test derives them).
    read(['train', 'pipeline', 'status'], running ? 'train-pipeline-status-resumes-list-running-handmade.json' : 'train-pipeline-status-resumes-list.json'),
    status(sweep.cyclePass, running ? 'train-pipeline-status-cycle-pass-running-handmade.json' : 'train-pipeline-status-cycle-pass.json'),
    status(sweep.keptFailed, 'train-pipeline-status-kept-failed.json'),
    status(sweep.specFileGone, 'train-pipeline-status-spec-file-gone.json'),
    status(sweep.held, held ? 'train-pipeline-status-named-held.json' : 'train-pipeline-status-named.json'),
  ]
  const sweepJobs = (): Rule[] => [
    job(['train', 'cycle', 'run'], { stdout: look('train-cycle-run-locked.jsonl'), exit: 1 }),
    refused(['train', 'pipeline', 'resume', sweep.cyclePass], 'train-pipeline-resume-cycle-pass.err'),
    refused(['train', 'pipeline', 'resume', sweep.specFileGone], 'train-pipeline-resume-no-spec.err'),
    refused(['train', 'pipeline', 'resume', sweep.held], 'train-pipeline-resume-held.err'),
  ]
  const afterKept = { arguments: ['train', 'cycle', 'resume'], answer: { stdout: look('train-cycle-resume-after-kept.txt') } }
  const afterBreaker = { arguments: ['train', 'cycle', 'resume'], answer: { stdout: look('train-cycle-resume.txt') } }
  switch (world) {
    case 'recorded':
      return {
        reads: [read(['train', 'status'], 'train-status.json'), read(['train', 'pipeline', 'list'], 'train-pipeline-list-none.json'), read(['train', 'pipeline', 'status'], 'train-pipeline-status-none.json')],
        // No training.cycle yet, so no breaker trips here: the only halt is one by hand.
        commands: [afterKept],
        // No training.cycle yet: a pass says so, as the CLI does.
        jobs: [job(['train', 'cycle', 'run'], { stdout: look('train-cycle-run-unconfigured.jsonl'), exit: 1 })],
        chats: [],
      }
    case 'pipelines':
      return {
        reads: [read(['train', 'status'], 'train-status.json'), read(['train', 'pipeline', 'list'], 'train-pipeline-list.json'), read(['train', 'pipeline', 'status'], 'train-pipeline-status-none.json'), status('pipe-20261026-074112', 'train-pipeline-status-aborted.json')],
        commands: [afterBreaker],
        jobs: [
          // 55h's recordings: the `skills` run failing its second gate (exit 2, as the Mac's tests end it), `nightly` passing, and the resume.
          job(['train', 'pipeline', 'run', '--pipeline', 'skills', ...stream], { stdout: look('train-pipeline-run.jsonl'), exit: 2 }, true),
          job(['train', 'pipeline', 'run', '--pipeline', 'nightly', ...stream], { stdout: look('train-pipeline-run-nightly.jsonl') }, true),
          job(['train', 'pipeline', 'resume', 'pipe-20261026-074112'], { stdout: look('train-pipeline-resume.jsonl') }),
          // The cycle's passes in the order one home recorded them: skipped, passed, then failed, halting it.
          job(['train', 'cycle', 'run'], [{ stdout: look('train-cycle-run-skipped.jsonl') }, { stdout: look('train-cycle-run-passed.jsonl') }, { stdout: look('train-cycle-run-failed.jsonl'), exit: 2 }]),
        ],
        chats: [],
      }
    case 'cycle':
      return {
        reads: [
          read(['train', 'status'], 'train-status-cycle.json'),
          read(['train', 'cycle', 'status'], 'train-cycle-status-passed.json'),
          read(['train', 'pipeline', 'list'], 'train-pipeline-list.json'),
          read(['train', 'pipeline', 'status'], 'train-pipeline-status-list.json'),
          status('pipe-20261026-074117', 'train-pipeline-status-stopped.json'),
          status('pipe-20261026-074112', 'train-pipeline-status-complete.json'),
        ],
        commands: [afterBreaker],
        // The next pass is the one that fails and trips the breaker.
        jobs: [job(['train', 'cycle', 'run'], { stdout: look('train-cycle-run-failed.jsonl'), exit: 2 })],
        chats: [],
      }
    case 'stalelock':
    case 'resumeheld':
      return {
        reads: [read(['train', 'status'], 'train-status-cycle-stale-lock.json'), read(['train', 'cycle', 'status'], 'train-cycle-status-stale-lock.json'), ...sweepReads(world === 'resumeheld', false)],
        commands: [afterKept],
        jobs: sweepJobs(),
        chats: [],
      }
    case 'unreadable':
      return {
        // 57h: `train cycle status` refuses an unreadable history; the place never asks it while `train status` says so.
        reads: [read(['train', 'status'], 'train-status-cycle-unreadable.json'), refused(['train', 'cycle', 'status'], 'train-cycle-status-unreadable.err'), ...sweepReads(false, false)],
        commands: [afterKept],
        jobs: sweepJobs(),
        chats: [],
      }
    case 'passrunning':
      return {
        reads: [read(['train', 'status'], 'train-status-cycle-live-lock.json'), read(['train', 'cycle', 'status'], 'train-cycle-status-live-lock.json'), ...sweepReads(false, true)],
        // A pass always holds this home, so a halt is taken during it: the CLI's during-pass line (after shared()'s, so it wins).
        commands: [{ arguments: ['train', 'cycle', 'halt'], answer: { stdout: look('train-cycle-halt-during-pass.txt') } }, afterKept],
        jobs: sweepJobs(),
        chats: [],
      }
  }
}

/** A world's rules: what every world answers, then its own, so its own answer to the same arguments is the one given. */
export function trainingWorld(world: TrainingWorld, look: Lookup): Rules {
  const base = shared(look)
  const mine = own(world, look)
  return { reads: [...base.reads, ...mine.reads], commands: [...base.commands, ...mine.commands], jobs: [...base.jobs, ...mine.jobs] }
}

/** The file names a world's rules name, in order: what the fixture check holds to exist. */
export function trainingWorldFiles(world: TrainingWorld): string[] {
  const named: string[] = []
  trainingWorld(world, (name) => {
    named.push(name)
    return ''
  })
  return named
}
