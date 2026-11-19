import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'
import mockActor from '@recorded/data/protocol/Fixtures/mock-actor.jsonl?raw'
import trainPipelineRun from '@recorded/business/jobs/Fixtures/train-pipeline-run.jsonl?raw'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadOutcome, ReadRunner } from '../../../source/business/reads/ReadRunner'
import { ApogeeClient } from '../../../source/business/session/ApogeeClient'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { Preset } from '../../../source/infrastructure/process/Preset'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'

/** A preset in each mode, over the shared mode rules, as a place's preview will build one. */
function standIn(apogee: ApogeeMode) {
  const preset: Preset = {
    rules: {
      reads: [{ arguments: ['agents', 'list'], answer: { stdout: agentsList } }],
      jobs: [{ arguments: ['train', 'pipeline', 'run'], answer: { stdout: trainPipelineRun, exit: 2 } }],
      chats: [{ arguments: ['chat'], answer: { stdout: mockActor } }],
    },
    route: 'library',
    clock: {},
    apogee,
  }
  const child = new StandInChild(preset, modeRules)
  const clock = new Clock(preset.clock)
  return { child, clock, reads: new ReadRunner(child), jobs: new JobStarter(child, clock), chats: new ApogeeClient(child, clock) }
}

const reads: ReadonlyArray<readonly string[]> = [['agents', 'list'], ['review', 'list'], ['system'], ['models', 'list', '--all']]

describe('the three modes, over the shared mode rules', () => {
  test('the config-typo refusal is agents-list-bad-config.err, word for word', () => {
    expect(modeRules.configTypoRefusal).toBe(agentsListBadConfig)
  })

  test('missing finds no binary and runs no read', async () => {
    const { child, reads: runner, jobs, chats } = standIn('missing')
    expect(await chats.locateBinary()).toEqual({ case: 'notFound' })
    for (const args of reads) {
      expect(await runner.read(StandInChild.binary, args)).toEqual(ReadOutcome.couldNotStart('apogee is not installed'))
    }
    expect(jobs.startJob(StandInChild.binary, ['train', 'pipeline', 'run'], true).updates.yielded.at(-1)?.case).toBe('ended')
    expect(child.calls).toEqual([])
  })

  test('config-typo fails every read with the recorded refusal, its verb swapped, as the old prototype says it', async () => {
    const { reads: runner, chats } = standIn('config-typo')
    expect(await chats.locateBinary()).toEqual({ case: 'found', binary: StandInChild.binary })
    for (const args of reads) {
      const outcome = await runner.read(StandInChild.binary, args)
      expect(outcome.case).toBe('failed')
      expect(ReadOutcome.failure(outcome)).toBe(
        `apogee ${args[0]}: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)`,
      )
    }
  })

  test('installed answers from the rules', async () => {
    const { reads: runner } = standIn('installed')
    expect(ReadOutcome.documentOf(await runner.read(StandInChild.binary, ['agents', 'list']))?.value).toEqual(JSON.parse(agentsList))
  })
})
