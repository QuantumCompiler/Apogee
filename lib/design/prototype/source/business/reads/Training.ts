import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/**
 * The Training place's reads (design 1j): the Mac app's `Training.swift`
 * (`BusinessReads`), its names and its rules, over the documents `train
 * status`, `train versions`, `train regime list`, `train pipeline list |
 * show | status` and `train cycle status`, `datasets kits` and `datasets
 * list` print with `--output-format json`. Fields this client does not know
 * are passed over.
 */

const { array, bool, int, number, object, string } = JSONValue

/** A string field's words: `undefined` for a null, an empty string, another kind of value, or none. */
function text(value: JSONValue | undefined): string | undefined {
  const words = string(value)
  return words === undefined || words === '' ? undefined : words
}

const field = (value: JSONValue | undefined, key: string): JSONValue | undefined => object(value)?.[key]

/** One training run, as `train status` records it: what was trained on what, how it ended, and its gate's verdict. */
export interface TrainingRun {
  readonly id: string
  readonly kind: string
  /** `complete`, `failed`, `running` …, in the CLI's word. */
  readonly status: string
  readonly method: string
  readonly trainer: string
  readonly iterations: number | undefined
  readonly finalLoss: number | undefined
  readonly baseModel: string
  readonly dataset: string
  readonly startedAt: string
  readonly finishedAt: string
  /** The gate's verdict and score (`train eval`), undefined until gated. */
  readonly evalPassed: boolean | undefined
  readonly evalScore: number | undefined
  /** Known by its id alone: `train status` names it running but gives the facts of its newest five alone (GUI 6i). */
  readonly listedByID: boolean
}

export const TrainingRun = {
  read(value: JSONValue): TrainingRun | undefined {
    const id = text(field(value, 'id'))
    if (id === undefined) return undefined
    return {
      id,
      kind: string(field(value, 'kind')) ?? '',
      status: string(field(value, 'status')) ?? '',
      method: string(field(value, 'method')) ?? '',
      trainer: string(field(value, 'trainer')) ?? '',
      iterations: int(field(value, 'iterations')),
      finalLoss: number(field(value, 'final_loss')),
      baseModel: string(field(value, 'base_model')) ?? '',
      dataset: string(field(value, 'dataset')) ?? '',
      startedAt: string(field(value, 'started_at')) ?? '',
      finishedAt: string(field(value, 'finished_at')) ?? '',
      evalPassed: bool(field(value, 'eval_passed')),
      evalScore: number(field(value, 'eval_score')),
      listedByID: false,
    }
  },

  /** A run `train status` lists as running by its id, with none of its facts. */
  runningID(id: string): TrainingRun {
    return { id, kind: '', status: 'running', method: '', trainer: '', iterations: undefined, finalLoss: undefined, baseModel: '', dataset: '', startedAt: '', finishedAt: '', evalPassed: undefined, evalScore: undefined, listedByID: true }
  },
}

/** One promoted version of a backend's ledger. */
export interface LedgerVersion {
  readonly version: number
  readonly runID: string
  /** The GGUF, an MLX version's directory, or an adapter version's adapter. */
  readonly path: string
  readonly promotedAt: string
  readonly evalScore: number | undefined
  readonly evalPassed: boolean | undefined
  readonly pruned: boolean
  /** The adapter itself is the version (`train promote --adapter`, 53b). */
  readonly isAdapter: boolean
  /** Where its gate scored it, when that was the base it serves on (53c). */
  readonly evalScoredOn: string | undefined
}

/** A backend's promoted versions, as `train versions` lists each ledger. */
export interface VersionLedger {
  readonly backend: string
  readonly active: number | undefined
  readonly versions: readonly LedgerVersion[]
  /** Whether the active version is an adapter; undefined with none active. The CLI never mixes the two on one backend. */
  readonly holdsAdapters: boolean | undefined
}

export const VersionLedger = {
  read(value: JSONValue): VersionLedger | undefined {
    const backend = string(field(value, 'backend_name'))
    if (backend === undefined) return undefined
    const active = int(field(value, 'active_version'))
    const versions = (array(field(value, 'versions')) ?? []).flatMap((entry): LedgerVersion[] => {
      const version = int(field(entry, 'version'))
      if (version === undefined) return []
      const adapter = string(field(entry, 'adapter_path'))
      return [
        {
          version,
          runID: string(field(entry, 'run_id')) ?? '',
          path: string(field(entry, 'gguf_path')) ?? string(field(entry, 'mlx_path')) ?? adapter ?? '',
          promotedAt: string(field(entry, 'promoted_at')) ?? '',
          evalScore: number(field(entry, 'eval_score')),
          evalPassed: bool(field(entry, 'eval_passed')),
          pruned: field(entry, 'pruned_at') !== undefined,
          isAdapter: adapter !== undefined,
          evalScoredOn: string(field(entry, 'eval_scored_on')),
        },
      ]
    })
    const current = versions.find((entry) => entry.version === active)
    return { backend, active, versions, holdsAdapters: active === undefined || current === undefined ? undefined : current.isAdapter }
  },

  list(document: ReadDocument): VersionLedger[] {
    return (document.entries ?? []).flatMap((entry) => VersionLedger.read(entry) ?? [])
  },
}

/** A pipeline run as `train pipeline status` lists it (55h): its spec, its status, how many of its stages passed. */
export interface PipelineRunRow {
  readonly id: string
  readonly specName: string
  readonly status: string
  readonly stages: number
  readonly passed: number
  readonly startedAt: string
  readonly completedAt: string | undefined
}

export const PipelineRunRow = {
  read(value: JSONValue): PipelineRunRow | undefined {
    const id = text(field(value, 'id'))
    if (id === undefined) return undefined
    return {
      id,
      specName: string(field(value, 'spec_name')) ?? '',
      status: string(field(value, 'status')) ?? '',
      stages: int(field(value, 'stages')) ?? 0,
      passed: int(field(value, 'passed')) ?? 0,
      startedAt: string(field(value, 'started_at')) ?? '',
      completedAt: string(field(value, 'completed_at')),
    }
  },

  /** The list, newest first as the CLI orders it. */
  list(document: ReadDocument): PipelineRunRow[] {
    return (document.entries ?? []).flatMap((entry) => PipelineRunRow.read(entry) ?? [])
  },

  /** A manifest as its row: the counts the list gives, from its stages. */
  of(run: PipelineRun): PipelineRunRow {
    return { id: run.id, specName: run.specName, status: run.status, stages: run.stages.length, passed: run.stages.filter((stage) => stage.status === 'passed').length, startedAt: run.startedAt, completedAt: run.completedAt }
  },
}

/** One stage as a manifest records it. */
export interface PipelineStage {
  /** From 1, as a run's `stage` events count; the manifest's own `index` counts from 0. */
  readonly index: number
  readonly name: string
  /** The stage's own run: what `train eval` and `train promote` take. */
  readonly runID: string
  /** `pending`, `training`, `evaluating`, `fusing`, `passed` or `failed`. */
  readonly status: string
  /** The cumulative gate's score, once this attempt was gated. */
  readonly score: number | undefined
  readonly dataset: string
}

/** Where `train pipeline resume` starts (57h): after the last stage that passed. */
export interface PipelineResume {
  readonly stage: number
  readonly name: string
  /** The `--pipeline` value that finds the run's spec now, or undefined when nothing does. */
  readonly pipeline: string | undefined
  /** The earlier stages that failed their gate and stay as they are, in the CLI's words. */
  readonly note: string | undefined
}

/** A pipeline run's manifest, whole: `train pipeline status <id>` (55h), `{"kind": "pipeline", "pipeline": {…}}`, or a run's closing document. */
export interface PipelineRun {
  readonly id: string
  readonly specName: string
  readonly student: string
  readonly baseModel: string
  readonly status: string
  readonly startedAt: string
  readonly completedAt: string | undefined
  readonly stages: readonly PipelineStage[]
  readonly resume: PipelineResume | undefined
  /** Whether the document carries `resume` at all, null or not: a binary from 57h on. */
  readonly hasResumeField: boolean
  readonly specRef: string | undefined
  /** The live process that runs it now (57g); 0 when its lock names none it can read. */
  readonly holder: number | undefined
  /** Whether `train pipeline resume` carries it on, by its status alone (before 57h). */
  readonly isResumable: boolean
  /** The first stage that has not passed: where an older binary's page said a resume starts. */
  readonly resumesAt: PipelineStage | undefined
  /** A cycle pass's pipeline, by its id's `cycle-` prefix (`kCycleIdPrefix`). */
  readonly isCyclePass: boolean
}

export const PipelineRun = {
  /** The CLI's `kCycleIdPrefix`. */
  cyclePassPrefix: 'cycle-',

  /** A run `train pipeline resume` continues: `aborted` or `failed`. */
  resumes(status: string): boolean {
    return status === 'aborted' || status === 'failed'
  },

  /** Whether a stage in `state` has been through its cumulative gate in this attempt. */
  isGated(state: string): boolean {
    return state === 'fusing' || state === 'passed' || state === 'failed'
  },

  /** The read's document, or a run's closing one: a pipeline's, never a plain run's. */
  fromDocument(document: ReadDocument): PipelineRun | undefined {
    return PipelineRun.fromValue(document.value)
  },

  fromValue(value: JSONValue): PipelineRun | undefined {
    if (string(field(value, 'kind')) !== 'pipeline') return undefined
    const pipeline = field(value, 'pipeline')
    const id = text(field(pipeline, 'pipeline_run_id'))
    if (id === undefined || pipeline === undefined) return undefined
    const around = object(value) ?? {}
    const resumeField = around.resume
    const resumeStage = int(field(resumeField, 'stage'))
    const resume: PipelineResume | undefined =
      resumeStage === undefined ? undefined : { stage: resumeStage, name: string(field(resumeField, 'name')) ?? '', pipeline: text(field(resumeField, 'pipeline')), note: text(field(resumeField, 'note')) }
    const holderField = around.holder
    const holder = holderField === undefined || object(holderField) === undefined ? undefined : (int(field(holderField, 'pid')) ?? 0)
    const status = string(field(pipeline, 'status')) ?? ''
    const stages = (array(field(pipeline, 'stages')) ?? []).map((stage, position): PipelineStage => {
      const state = string(field(stage, 'status')) ?? ''
      const gated = PipelineRun.isGated(state) && field(stage, 'eval_results') !== undefined
      return {
        index: (int(field(stage, 'index')) ?? position) + 1,
        name: string(field(stage, 'name')) ?? '',
        runID: string(field(stage, 'run_id')) ?? '',
        status: state,
        score: gated ? number(field(stage, 'cumulative_score')) : undefined,
        dataset: string(field(stage, 'dataset')) ?? '',
      }
    })
    return {
      id,
      specName: string(field(pipeline, 'spec_name')) ?? '',
      student: string(field(pipeline, 'student')) ?? '',
      baseModel: string(field(pipeline, 'base_model')) ?? '',
      status,
      startedAt: string(field(pipeline, 'started_at')) ?? '',
      completedAt: string(field(pipeline, 'completed_at')),
      stages,
      resume,
      hasResumeField: resumeField !== undefined,
      specRef: text(around.spec_ref),
      holder,
      isResumable: PipelineRun.resumes(status),
      resumesAt: stages.find((stage) => stage.status !== 'passed'),
      isCyclePass: id.startsWith(PipelineRun.cyclePassPrefix),
    }
  },
}

/** One stage of a named pipeline's plan, as the spec writes it. */
export interface PipelineSpecStage {
  readonly index: number
  readonly name: string
  readonly dataset: string
  readonly evalSuite: string
  /** The suites its cumulative gate holds it to: every earlier stage's, then its own. */
  readonly gate: readonly string[]
}

/** A named pipeline as `train pipeline list | show` states it (the CLI's M30). */
export interface PipelineSpec {
  readonly name: string
  /** The spec file it was read from; undefined for a `training.pipelines` entry. */
  readonly path: string | undefined
  readonly student: string
  readonly stages: readonly PipelineSpecStage[]
}

export const PipelineSpec = {
  read(value: JSONValue): PipelineSpec | undefined {
    const name = text(field(value, 'name'))
    if (name === undefined) return undefined
    return {
      name,
      path: string(field(value, 'path')),
      student: string(field(value, 'student')) ?? '',
      stages: (array(field(value, 'stages')) ?? []).map((stage, position) => ({
        index: position + 1,
        name: string(field(stage, 'name')) ?? '',
        dataset: string(field(stage, 'dataset')) ?? '',
        evalSuite: string(field(stage, 'eval_suite')) ?? '',
        gate: (array(field(stage, 'gate')) ?? []).flatMap((suite) => string(suite) ?? []),
      })),
    }
  },

  list(document: ReadDocument): PipelineSpec[] {
    return (document.entries ?? []).flatMap((entry) => PipelineSpec.read(entry) ?? [])
  },
}

/** The cycle's headline in `train status`: where the loop stands, without its passes. */
export interface CycleHeadline {
  readonly backend: string
  readonly anchorVersion: number
  readonly anchorScore: number | undefined
  readonly consecutiveFails: number
  readonly totalRuns: number
  readonly halted: boolean
  readonly haltReason: string
}

function headline(value: JSONValue | undefined): CycleHeadline | undefined {
  if (object(value) === undefined) return undefined
  return {
    backend: string(field(value, 'backend')) ?? '',
    anchorVersion: int(field(value, 'anchor_version')) ?? 0,
    anchorScore: number(field(value, 'anchor_score')),
    consecutiveFails: int(field(value, 'consecutive_fails')) ?? 0,
    totalRuns: int(field(value, 'total_runs')) ?? 0,
    halted: bool(field(value, 'halted')) ?? false,
    haltReason: string(field(value, 'halt_reason')) ?? '',
  }
}

/** `train status`: the runs, what is running, the pipelines and the cycle. */
export class TrainingStatus {
  /** The newest five runs, each with its facts. */
  readonly recent: readonly TrainingRun[]
  /** Every run training now, by id, in the CLI's order. */
  readonly runningIDs: readonly string[]
  readonly runs: number
  readonly pipelines: number
  /** Whether a live pass holds the cycle's lock. */
  readonly cycleActive: boolean
  /** The lock a dead pass left (57g), or undefined. */
  readonly staleLock: string | undefined
  /** Whether the document carries `stale_lock` at all, null or not (57g). */
  readonly statesLockLiveness: boolean
  /** Why the cycle's history cannot be read, naming its file (57h). */
  readonly cycleError: string | undefined
  readonly activePipeline: string | undefined
  readonly latestPipeline: PipelineRunRow | undefined
  /** Undefined until a pass (or a halt) has written the history: read this before `train cycle status`. */
  readonly cycle: CycleHeadline | undefined

  constructor(document: ReadDocument) {
    const v = document.value
    this.recent = (array(v.recent_runs) ?? []).flatMap((entry) => TrainingRun.read(entry) ?? [])
    this.runningIDs = (array(v.running) ?? []).flatMap((entry) => text(entry) ?? text(field(entry, 'id')) ?? [])
    this.runs = int(v.runs) ?? 0
    this.pipelines = int(v.pipelines) ?? 0
    this.cycleActive = bool(v.cycle_active) ?? false
    this.staleLock = text(v.stale_lock)
    this.statesLockLiveness = v.stale_lock !== undefined
    this.cycleError = text(v.cycle_error)
    this.activePipeline = string(v.active_pipeline)
    this.latestPipeline = v.latest_pipeline === undefined ? undefined : PipelineRunRow.read(v.latest_pipeline)
    this.cycle = headline(v.cycle)
  }
}

/** One pass as the cycle's history records it. */
export interface CyclePass {
  /** Its place in the history, from 1. */
  readonly id: number
  readonly runAt: string
  /** The source its data came from; empty for a pass that found none. */
  readonly source: string
  readonly datasetRows: number
  readonly pipelineID: string
  /** `pass`, `fail` or `skipped`. */
  readonly gate: string
  readonly gateReason: string
  readonly cycleScore: number | undefined
  readonly previousScore: number | undefined
  readonly anchorScore: number | undefined
  /** The version it promoted; 0 for none. */
  readonly promotedVersion: number
  readonly note: string | undefined
}

/** The cycle's history: `train cycle status` (55h), or a pass's closing document. */
export interface CycleHistory extends CycleHeadline {
  readonly sessionsUntil: string
  /** Oldest first. */
  readonly runs: readonly CyclePass[]
  readonly active: boolean
  readonly staleLock: string | undefined
  readonly statesLockLiveness: boolean
}

export const CycleHistory = {
  fromDocument(document: ReadDocument): CycleHistory | undefined {
    return CycleHistory.fromValue(document.value)
  },

  fromValue(value: JSONValue): CycleHistory | undefined {
    const v = object(value)
    const head = headline(value)
    if (v === undefined || head === undefined || (v.halted === undefined && v.runs === undefined)) return undefined
    return {
      ...head,
      sessionsUntil: string(v.sessions_until) ?? '',
      active: bool(v.active) ?? false,
      staleLock: text(v.stale_lock),
      statesLockLiveness: v.stale_lock !== undefined,
      runs: (array(v.runs) ?? []).map((pass, position) => ({
        id: position + 1,
        runAt: string(field(pass, 'run_at')) ?? '',
        source: string(field(pass, 'source')) ?? '',
        datasetRows: int(field(pass, 'dataset_rows')) ?? 0,
        pipelineID: string(field(pass, 'pipeline_id')) ?? '',
        gate: string(field(pass, 'gate')) ?? '',
        gateReason: string(field(pass, 'gate_reason')) ?? '',
        cycleScore: number(field(pass, 'cycle_score')),
        previousScore: number(field(pass, 'prev_score')),
        anchorScore: number(field(pass, 'anchor_score')),
        promotedVersion: int(field(pass, 'promoted_version')) ?? 0,
        note: text(field(pass, 'note')),
      })),
    }
  },
}

/** A regime, as `train regime list` carries it: its kits in order. */
export interface TrainingRegime {
  readonly name: string
  readonly description: string
  readonly kits: readonly string[]
  /** `shipped`, `seeded` or `config`. */
  readonly source: string
}

export const TrainingRegime = {
  list(document: ReadDocument): TrainingRegime[] {
    return (document.entries ?? []).flatMap((value) => {
      const name = string(field(value, 'name'))
      return name === undefined ? [] : [{ name, description: string(field(value, 'description')) ?? '', kits: (array(field(value, 'kits')) ?? []).flatMap((kit) => string(kit) ?? []), source: string(field(value, 'source')) ?? '' }]
    })
  },
}

/** A training kit (`datasets kits`): a skill, and the eval items it gates on. */
export interface TrainingKit {
  readonly name: string
  readonly description: string
  readonly evalItems: number
}

export const TrainingKit = {
  list(document: ReadDocument): TrainingKit[] {
    return (document.entries ?? []).flatMap((value) => {
      const name = string(field(value, 'name'))
      return name === undefined ? [] : [{ name, description: string(field(value, 'description')) ?? '', evalItems: int(field(value, 'eval_items')) ?? 0 }]
    })
  },
}

/** A dataset (`datasets list`): its examples and their shape. */
export interface TrainingDataset {
  readonly name: string
  readonly lines: number
  readonly bytes: number
  /** `chat`, `completion` …, as the CLI reads it. */
  readonly shape: string
  readonly path: string
}

export const TrainingDataset = {
  list(document: ReadDocument): TrainingDataset[] {
    return (document.entries ?? []).flatMap((value) => {
      const name = string(field(value, 'name'))
      return name === undefined ? [] : [{ name, lines: int(field(value, 'lines')) ?? 0, bytes: int(field(value, 'bytes')) ?? 0, shape: string(field(value, 'shape')) ?? '', path: string(field(value, 'path')) ?? '' }]
    })
  },
}
