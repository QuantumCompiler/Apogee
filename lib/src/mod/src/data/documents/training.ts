// `apogee train status --output-format json` (machine-mode.md, "train
// status"): the runs, each backend's versions headline, the pipelines and
// the cycle -- byte for byte the control plane's training status body.
import { decode } from './document'
import type { Decoded, Reader } from './document'

/** One training run, as the status lists it. */
export type RunSummary = {
  id: string
  /** `running`, `complete`, `failed`, `cancelled`, ... */
  status: string
  method?: string
  startedAt?: string
  finishedAt?: string
}

/** One backend's versions headline: which promoted version it runs. */
export type VersionHeadline = { backend: string; activeVersion?: number; kept?: number; total?: number }

/** The newest pipeline run. */
export type PipelineHeadline = { id: string; specName?: string; status?: string; stages?: number; passed?: number }

/** The cycle's headline, once a pass has written its history. */
export type CycleHeadline = { backend?: string; isHalted?: boolean; haltReason?: string }

/** The training status. */
export type TrainStatus = {
  /** The ids of the runs running now. */
  running: string[]
  recentRuns: RunSummary[]
  versions: VersionHeadline[]
  /** The pipeline a live process runs now, if any. */
  activePipeline?: string
  /** Whether a running cycle pass holds the cycle's lock. */
  isCycleActive?: boolean
  runs?: number
  pipelines?: number
  latestPipeline?: PipelineHeadline
  cycle?: CycleHeadline
}

/** The training status's document, read tolerantly. */
export function decodeTrainStatus(text: string): Decoded<TrainStatus> {
  return decode(text, top => {
    const running = top.texts('running')
    const recentRuns = top.list('recent_runs', runOf)
    const versions = top.list('versions', version => {
      const one: VersionHeadline = { backend: version.text('backend') }
      const activeVersion = version.number('active_version')
      const kept = version.optionalNumber('kept')
      const total = version.optionalNumber('total')

      return {
        ...one,
        ...(activeVersion === undefined ? {} : { activeVersion }),
        ...(kept === undefined ? {} : { kept }),
        ...(total === undefined ? {} : { total }),
      }
    })
    const activePipeline = top.nullableText('active_pipeline')
    const isCycleActive = top.flag('cycle_active')
    const runs = top.optionalNumber('runs')
    const pipelines = top.optionalNumber('pipelines')
    const latestPipeline = top.optionalObject('latest_pipeline', pipelineOf)
    const cycle = top.optionalObject('cycle', cycleOf)

    return {
      running,
      recentRuns,
      versions,
      ...(activePipeline === undefined ? {} : { activePipeline }),
      ...(isCycleActive === undefined ? {} : { isCycleActive }),
      ...(runs === undefined ? {} : { runs }),
      ...(pipelines === undefined ? {} : { pipelines }),
      ...(latestPipeline === undefined ? {} : { latestPipeline }),
      ...(cycle === undefined ? {} : { cycle }),
    }
  })
}

function runOf(run: Reader): RunSummary {
  const one: RunSummary = { id: run.text('id'), status: run.text('status') }
  const method = run.optionalText('method')
  const startedAt = run.optionalText('started_at')
  const finishedAt = run.optionalText('finished_at')

  return {
    ...one,
    ...(method === undefined ? {} : { method }),
    ...(startedAt === undefined ? {} : { startedAt }),
    ...(finishedAt === undefined ? {} : { finishedAt }),
  }
}

function pipelineOf(pipeline: Reader): PipelineHeadline {
  const one: PipelineHeadline = { id: pipeline.text('id') }
  const specName = pipeline.optionalText('spec_name')
  const status = pipeline.optionalText('status')
  const stages = pipeline.optionalNumber('stages')
  const passed = pipeline.optionalNumber('passed')

  return {
    ...one,
    ...(specName === undefined ? {} : { specName }),
    ...(status === undefined ? {} : { status }),
    ...(stages === undefined ? {} : { stages }),
    ...(passed === undefined ? {} : { passed }),
  }
}

function cycleOf(cycle: Reader): CycleHeadline {
  const backend = cycle.optionalText('backend')
  const isHalted = cycle.optionalFlag('halted')
  const haltReason = cycle.optionalText('halt_reason')

  return {
    ...(backend === undefined ? {} : { backend }),
    ...(isHalted === undefined ? {} : { isHalted }),
    ...(haltReason === undefined ? {} : { haltReason }),
  }
}
