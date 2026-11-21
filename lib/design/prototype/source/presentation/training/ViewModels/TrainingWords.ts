import type { CycleHeadline, CyclePass, PipelineRun, PipelineRunRow, PipelineSpec, TrainingRun } from '../../../business/reads/Training'
import type { StageRow } from './TrainingViewModel'

/** A score, 0 to 1, as a whole percent. */
const percent = (score: number) => `${Math.round(score * 100)}%`

/**
 * What the Training place says, in the old prototype's words where it has
 * them (`main.html` at `b375fac9`, `training` section, 6634-6806) and the
 * Mac's where the old says nothing (TrainingViewModel.swift): each a function
 * of the CLI's documents, never a number of its own.
 */
export const TrainingWords = {
  missing: "Apogee isn't installed, so there is nothing to train.",
  /** The old bar's word before a run has said anything, and a live run's id until its stream names it. */
  starting: 'Starting…',
  noSpecs: "no named pipelines -- add one under training.pipelines, or run a spec file with 'apogee train pipeline run --pipeline <file>'",
  noRuns: "no pipeline runs yet ('apogee train pipeline run --pipeline <spec>')",
  schedule: 'The cycle is unattended by design: your own scheduler -- launchd or cron -- runs apogee train cycle run. Nothing here schedules it; Run one pass is a single pass, now.',
  noHistory: 'No pass has run yet, so the cycle has no history: its first pass writes one -- Run one pass here, or your scheduler’s first run. A pass needs training.cycle in your config; the run says so when it’s missing.',
  unreadable: 'Run one pass, Halt and Resume are held: each refuses while the history cannot be read.',
  eachStage: 'Each stage is a training run, held to the cumulative gate -- this machine is held until it ends.',
  kept: 'The pass running now finishes, and keeps the halt',
  lockRemedy: 'If no pass is running, one that died left its lock: remove training/cycle/cycle.lock under your Apogee home.',
  listedByID: 'still training -- train status names it by its id alone, as it is older than the five newest runs whose facts it gives',

  /**
   * A pass's word that a halt taken while it ran is kept: the CLI's
   * `halt_kept_notice` (`cycle.cpp`, M30), the old prototype's `kept`.
   */
  haltKept(reason: string): string {
    return `[cycle] halted while this pass ran (${reason === '' ? "'apogee train cycle halt' was run" : reason}) -- the halt is kept: 'apogee train cycle run' refuses until 'apogee train cycle resume'`
  },

  /** The gate's verdict, said plainly. */
  gate(run: TrainingRun): string {
    const score = run.evalScore === undefined ? '' : ` (${percent(run.evalScore)})`
    if (run.evalPassed === true) return `passed the gate${score}`
    if (run.evalPassed === false) return `failed the gate${score} -- promote refuses it unless forced`
    return 'not gated yet'
  },

  /** A run's line under its id: its method, trainer and loss, and its gate. */
  summary(run: TrainingRun): string {
    if (run.listedByID) return TrainingWords.listedByID
    const facts = [run.method, run.trainer, run.finalLoss === undefined ? '' : `loss ${run.finalLoss.toFixed(4)}`].filter((fact) => fact !== '')
    return [...facts, TrainingWords.gate(run)].join(' · ')
  },

  /** A named pipeline in a line: `2 stages · student tiny`. */
  spec(spec: PipelineSpec): string {
    const stages = `${spec.stages.length} ${spec.stages.length === 1 ? 'stage' : 'stages'}`
    return spec.student === '' ? stages : `${stages} · student ${spec.student}`
  },

  /** A pipeline run in a line: its spec, its status, its stages passed. */
  row(row: PipelineRunRow): string {
    return [row.specName, row.status, `${row.passed} of ${row.stages} ${row.stages === 1 ? 'stage' : 'stages'} passed`].filter((part) => part !== '').join(' · ')
  },

  /** A stage in a line: its word, the gate's score once gated, the trainer's facts while it trains. */
  stage(stage: StageRow): string {
    const parts = [stage.state]
    if (stage.score !== undefined) parts.push(`cumulative ${percent(stage.score)}`)
    const iteration = stage.iteration
    if (iteration) {
      parts.push(`iter ${iteration.iteration}${iteration.iterations === undefined ? '' : `/${iteration.iterations}`}`)
      if (iteration.loss !== undefined) parts.push(`loss ${iteration.loss.toFixed(4)}`)
      if (iteration.throughput !== undefined) parts.push(`${iteration.throughput.toFixed(1)} it/s`)
    }
    return parts.join(' · ')
  },

  /** Why a resume is held while a live process runs it (57g). */
  heldResume(holder: number): string {
    return `This pipeline is running now (${holder > 0 ? `process ${holder}` : 'its lock names no process it can read'}); a resume is refused while it runs.`
  },

  /** Why a resume is held where no `--pipeline` finds its spec (57h). */
  resumeNeedsSpec(id: string): string {
    return `Resume needs its spec: run 'apogee train pipeline resume ${id} --pipeline <spec>' in the console.`
  },

  /** The run page's last line: an ordinary pipeline's, or a cycle pass's as 57h says it. */
  pageLine(run: PipelineRun): string {
    if (!run.isCyclePass) return 'Each stage’s run is an ordinary run: gate it and promote it from Runs.'
    if (run.status === 'complete') return 'A cycle pass: its candidate is the cycle’s, judged by its dual gate -- promoted on a pass, discarded on a fail, never from Runs. Run one pass on the Cycle tab runs the next pass.'
    if (run.status === 'running' && (run.holder !== undefined || !run.hasResumeField)) {
      const now = run.holder === undefined ? 'running as its manifest says' : `running now (${run.holder > 0 ? `process ${run.holder}` : 'its lock names no process it can read'})`
      return `A cycle pass, ${now}: its candidate is the cycle’s, judged by its dual gate when the pass ends -- never from Runs.`
    }
    return 'A cycle pass: its candidate was discarded, and no other verb resumes it -- Run one pass on the Cycle tab runs the next pass.'
  },

  /** A dead pass's lock in the CLI's words (57g), and why Run one pass waits. */
  staleLock(path: string): string {
    return `Stopped -- cycle.lock is left by a process that is not running; remove ${path} to run again. Until then every pass refuses -- Run one pass here, and your scheduler’s.`
  },

  /** The anchor in a line, as `train cycle status` says it. */
  anchor(cycle: CycleHeadline): string {
    return cycle.anchorVersion > 0 ? `v${cycle.anchorVersion}${cycle.anchorScore === undefined ? '' : ` (${percent(cycle.anchorScore)})`}` : 'not set -- the first passing pass sets it'
  },

  /** A pass in a line: its gate, its score against the last pass and the anchor, what it promoted, where its data came from. A score of 0 is one never reached. */
  pass(pass: CyclePass): string {
    const reached = (score: number | undefined) => (score === undefined || score === 0 ? undefined : score)
    const parts = [pass.gate]
    const score = reached(pass.cycleScore)
    if (score !== undefined) {
      const against = (
        [
          ['previous', reached(pass.previousScore)],
          ['anchor', reached(pass.anchorScore)],
        ] as const
      ).flatMap(([name, value]) => (value === undefined ? [] : [`${name} ${percent(value)}`]))
      parts.push(`scored ${percent(score)}${against.length > 0 ? ` (${against.join(', ')})` : ''}`)
    }
    if (pass.promotedVersion > 0) parts.push(`promoted v${pass.promotedVersion}`)
    if (pass.source !== '') parts.push(`${pass.datasetRows} ${pass.datasetRows === 1 ? 'example' : 'examples'} from ${pass.source}`)
    return parts.join(' · ')
  },

  /** The CLI's words as a sentence: a capital first, a stop last. */
  sentence(words: string): string {
    if (words === '') return words
    return words.charAt(0).toUpperCase() + words.slice(1) + (words.endsWith('.') ? '' : '.')
  },
}
