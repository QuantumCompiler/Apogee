import trainStatus from '@recorded/business/reads/Fixtures/train-status.json?raw'
import trainStatusCycle from '@recorded/business/reads/Fixtures/train-status-cycle.json?raw'
import trainStatusStaleLock from '@recorded/business/reads/Fixtures/train-status-cycle-stale-lock.json?raw'
import trainStatusUnreadable from '@recorded/business/reads/Fixtures/train-status-cycle-unreadable.json?raw'
import trainStatusRunningOlder from '@recorded/business/reads/Fixtures/train-status-running-older.json?raw'
import trainVersions from '@recorded/business/reads/Fixtures/train-versions-adapters.json?raw'
import trainRegimes from '@recorded/business/reads/Fixtures/train-regimes.json?raw'
import datasetsKits from '@recorded/business/reads/Fixtures/datasets-kits.json?raw'
import datasetsList from '@recorded/business/reads/Fixtures/datasets-list.json?raw'
import pipelineList from '@recorded/business/reads/Fixtures/train-pipeline-list.json?raw'
import pipelineStatusList from '@recorded/business/reads/Fixtures/train-pipeline-status-list.json?raw'
import pipelineAborted from '@recorded/business/reads/Fixtures/train-pipeline-status-aborted.json?raw'
import pipelineKeptFailed from '@recorded/business/reads/Fixtures/train-pipeline-status-kept-failed.json?raw'
import pipelineNamedHeld from '@recorded/business/reads/Fixtures/train-pipeline-status-named-held.json?raw'
import pipelineCyclePass from '@recorded/business/reads/Fixtures/train-pipeline-status-cycle-pass.json?raw'
import pipelineSpecFileGone from '@recorded/business/reads/Fixtures/train-pipeline-status-spec-file-gone.json?raw'
import cyclePassed from '@recorded/business/reads/Fixtures/train-cycle-status-passed.json?raw'
import cycleStaleLock from '@recorded/business/reads/Fixtures/train-cycle-status-stale-lock.json?raw'
import cycleHalted from '@recorded/business/reads/Fixtures/train-cycle-status-halted.json?raw'
import { describe, expect, test } from 'vitest'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import {
  CycleHistory,
  PipelineRun,
  PipelineRunRow,
  PipelineSpec,
  TrainingDataset,
  TrainingKit,
  TrainingRegime,
  TrainingStatus,
  VersionLedger,
} from '../../../source/business/reads/Training'

const document = (text: string) => {
  const read = ReadDocument.from(text)
  if (!read) throw new Error('not a document')
  return read
}

describe("the Training place's reads (the Mac's Training.swift), each over its recording", () => {
  test('train-status.json: one recent run with its facts and gate, nothing running, no cycle yet', () => {
    const status = new TrainingStatus(document(trainStatus))
    expect(status.recent).toHaveLength(1)
    const run = status.recent[0]
    expect(run?.id).toBe('20261021-051826')
    expect([run?.status, run?.method, run?.trainer, run?.iterations, run?.finalLoss]).toEqual(['complete', 'lora', 'mock', 4, 0.42])
    expect([run?.baseModel, run?.dataset]).toEqual(['/Users/someone/work/student', '/Users/someone/work/data.jsonl'])
    expect([run?.evalPassed, run?.evalScore]).toEqual([false, 0.4583333333333333])
    expect([run?.startedAt, run?.finishedAt]).toEqual(['2026-10-21T05:18:26Z', '2026-10-21T05:18:26Z'])
    expect(status.runningIDs).toEqual([])
    expect(status.cycle).toBeUndefined()
    expect(status.statesLockLiveness).toBe(false)
    expect(status.cycleError).toBeUndefined()
  })

  test("train-status-cycle.json: the cycle's headline -- anchor v1 at 100%, two passes, not halted", () => {
    const status = new TrainingStatus(document(trainStatusCycle))
    expect(status.cycle).toEqual({ backend: 'nightly-model', anchorVersion: 1, anchorScore: 1, consecutiveFails: 0, totalRuns: 2, halted: false, haltReason: '' })
    expect(status.latestPipeline?.id).toBe('cycle-20261026-074119')
  })

  test("57g's dead pass's lock, 57h's unreadable history, and 6i's running id older than the five newest", () => {
    const stale = new TrainingStatus(document(trainStatusStaleLock))
    expect(stale.staleLock).toBe('/Users/someone/.apogee/training/cycle/cycle.lock')
    expect(stale.statesLockLiveness).toBe(true)
    const unreadable = new TrainingStatus(document(trainStatusUnreadable))
    expect(unreadable.cycle).toBeUndefined()
    expect(unreadable.cycleError).toBe('/Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array')
    expect(unreadable.staleLock).toBeUndefined()
    expect(unreadable.statesLockLiveness).toBe(true)
    expect(new TrainingStatus(document(trainStatusRunningOlder)).runningIDs).toEqual(['pipe-20261109-040503-s0'])
  })

  test("train-versions-adapters.json: three ledgers, two holding adapter versions, one merged, q4's scored on its quantized base", () => {
    const ledgers = VersionLedger.list(document(trainVersions))
    expect(ledgers.map((ledger) => [ledger.backend, ledger.active, ledger.holdsAdapters])).toEqual([
      ['based', 1, true],
      ['q4', 1, true],
      ['tuned', 1, false],
    ])
    expect(ledgers[1]?.versions[0]).toMatchObject({ version: 1, runID: '20261026-065638', isAdapter: true, evalScoredOn: 'tiny-Q4_K_M.gguf (Q4_K_M)' })
    expect(ledgers[2]?.versions[0]?.path).toBe('/Users/someone/.apogee/models/tiny/gguf/cf368ccf9b45/tuned-v1.gguf')
  })

  test('the regimes, the kits and the datasets, as listed', () => {
    const regimes = TrainingRegime.list(document(trainRegimes))
    expect(regimes.map((regime) => [regime.name, regime.kits.length, regime.source])).toEqual([
      ['foundation', 4, 'seeded'],
      ['harness-helpers', 3, 'seeded'],
      ['knowledge', 1, 'seeded'],
    ])
    expect(TrainingKit.list(document(datasetsKits)).map((kit) => [kit.name, kit.evalItems]).at(-2)).toEqual(['tool-calling', 32])
    expect(TrainingDataset.list(document(datasetsList))).toEqual([{ name: 'heat-notes', lines: 2, bytes: 449, shape: 'chat', path: '/Users/someone/.apogee/training/datasets/heat-notes.jsonl' }])
  })

  test('train-pipeline-list.json: the named specs, each its student and stage plan', () => {
    const specs = PipelineSpec.list(document(pipelineList))
    expect(specs.map((spec) => [spec.name, spec.student, spec.stages.map((stage) => stage.name)])).toEqual([
      ['nightly', 'tiny', ['base']],
      ['skills', 'tiny', ['greetings', 'refusals']],
    ])
    expect(specs[1]?.stages[1]?.gate).toEqual(['hello', 'nope'])
  })

  test('train-pipeline-status-list.json: four runs, newest first, each its stages passed', () => {
    const rows = PipelineRunRow.list(document(pipelineStatusList))
    expect(rows.map((row) => [row.id, row.specName, row.status, row.passed, row.stages])).toEqual([
      ['pipe-20261026-074117', 'skills', 'aborted', 1, 2],
      ['pipe-20261026-074116', 'skills', 'complete', 2, 2],
      ['pipe-20261026-074115', 'nightly', 'complete', 1, 1],
      ['pipe-20261026-074112', 'skills', 'complete', 2, 2],
    ])
  })

  test("train-pipeline-status-aborted.json, before 57h: no resume field -- resumed, by its status, from the first stage that has not passed", () => {
    const run = PipelineRun.fromDocument(document(pipelineAborted))
    expect(run?.hasResumeField).toBe(false)
    expect(run?.isResumable).toBe(true)
    expect(run?.resumesAt).toMatchObject({ index: 2, name: 'refusals', status: 'failed', score: 0.5 })
    expect(run?.stages.map((stage) => [stage.index, stage.name, stage.status, stage.runID, stage.score])).toEqual([
      [1, 'greetings', 'passed', 'pipe-20261026-074112-s0', 1],
      [2, 'refusals', 'failed', 'pipe-20261026-074112-s1', 0.5],
    ])
  })

  test("57h's resume and 57g's holder, as the documents say them; a cycle pass by its id", () => {
    const kept = PipelineRun.fromDocument(document(pipelineKeptFailed))
    expect(kept?.resume).toEqual({ stage: 3, name: 'c', pipeline: 'three', note: 'stage 1, a, failed its gate and stays as it is' })
    // A stage not gated in this attempt has no score, though its manifest keeps a number.
    expect(kept?.stages[2]?.score).toBeUndefined()
    const held = PipelineRun.fromDocument(document(pipelineNamedHeld))
    expect([held?.holder, held?.specName, held?.specRef]).toEqual([67959, 'Nightly SFT', 'nightly'])
    const gone = PipelineRun.fromDocument(document(pipelineSpecFileGone))
    expect(gone?.resume).toEqual({ stage: 2, name: 'b', pipeline: undefined, note: undefined })
    const pass = PipelineRun.fromDocument(document(pipelineCyclePass))
    expect([pass?.isCyclePass, pass?.hasResumeField, pass?.resume, pass?.holder]).toEqual([true, true, undefined, undefined])
  })

  test("the cycle's history: train-cycle-status-passed.json, the stale lock, and the breaker's halt", () => {
    const passed = CycleHistory.fromDocument(document(cyclePassed))
    expect(passed?.runs.map((pass) => [pass.id, pass.gate, pass.cycleScore, pass.promotedVersion, pass.source, pass.datasetRows])).toEqual([
      [1, 'skipped', 0, 0, '', 0],
      [2, 'pass', 1, 1, 'directory', 2],
    ])
    expect(passed?.runs[0]?.gateReason).toBe('no new training data in any source')
    expect(passed?.statesLockLiveness).toBe(false)
    const stale = CycleHistory.fromDocument(document(cycleStaleLock))
    expect(stale?.staleLock).toBe('/Users/someone/.apogee/training/cycle/cycle.lock')
    expect(stale?.runs[0]?.note).toBe('circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1')
    const halted = CycleHistory.fromDocument(document(cycleHalted))
    expect([halted?.halted, halted?.consecutiveFails, halted?.haltReason]).toEqual([true, 1, 'circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1'])
  })

  test('a document of another read is none of these', () => {
    expect(PipelineRun.fromDocument(document(trainStatus))).toBeUndefined()
    expect(CycleHistory.fromDocument(document(datasetsList))).toBeUndefined()
  })
})
