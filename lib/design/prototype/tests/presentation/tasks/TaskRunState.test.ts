import taskRun from '@recorded/presentation/tasks/Fixtures/task-run.jsonl?raw'
import taskRunUses from '@recorded/presentation/tasks/Fixtures/task-run-uses.jsonl?raw'
import taskResume from '@recorded/presentation/tasks/Fixtures/task-resume.jsonl?raw'
import runRefused from '@recorded/presentation/tasks/Fixtures/task-run-refused.err?raw'
import { describe, expect, test } from 'vitest'
import { JobEnd, JobUpdate } from '../../../source/business/jobs/SupervisedJob'
import { JobEvent } from '../../../source/data/protocol/JobEvent'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'
import { TaskRunState } from '../../../source/presentation/tasks/ViewModels/TaskRunState'

const lines = (text: string) => text.split('\n').filter((line) => line !== '')
const fold = (text: string, count?: number, end?: JobEnd) => {
  const state = new TaskRunState()
  for (const line of lines(text).slice(0, count)) state.apply(JobUpdate.event(JobEvent.decode(line)))
  if (end) state.apply(JobUpdate.ended(end))
  return state
}

describe('a live run folded from its stream, and nothing else: the Mac’s TaskRunState (design 1p)', () => {
  test('task-run.jsonl whole: its task, its plan, every turn in order, its checks, its rounds used and how it finished', () => {
    const state = fold(taskRun, undefined, JobEnd.of(ChildExit.exited(1), new BoundedTail(), false))
    expect(state.taskID).toBe('task-20261021-011456')
    expect(state.plan).toBe('mock response')
    expect(state.orderedRounds.map((round) => [round.round, round.kind, round.outcome])).toEqual([
      [0, 'plan', 'completed'],
      [1, 'execute', 'completed'],
      [2, 'correct', 'completed'],
    ])
    expect(state.checks.map((check) => [check.description, check.passed])).toEqual([['the answer contains "mock"', true]])
    expect(state.roundsUsed).toBe(2)
    expect(state.finished).toEqual({ status: 'exhausted', reason: 'the round budget (2) is spent and the task is not done -- not reported done by the model' })
    expect(state.status).toBe('exhausted')
    expect(state.view?.status).toBe('exhausted')
    expect(state.refusal).toBeUndefined()
    expect(state.lastAt).toBe('2026-10-21T01:14:56Z')
  })

  test('mid-run, sixteen lines in: running, the plan recorded and round 1 in flight, its answer streaming', () => {
    const state = fold(taskRun, 16)
    expect(state.status).toBe('running')
    expect(state.orderedRounds.map((round) => [round.round, round.outcome])).toEqual([
      [0, 'completed'],
      [1, 'in_flight'],
    ])
    expect(state.answer).toBe('mock res')
    expect(state.finished).toBeUndefined()
  })

  test('task-run-uses.jsonl: a call its grant let through is held until its round lands; each tool call by its last state', () => {
    const allLines = lines(taskRunUses)
    const grantAt = allLines.findIndex((line) => line.includes('"type":"task_grant"'))
    const midway = fold(taskRunUses, grantAt + 1)
    expect(midway.grants).toEqual([{ round: 1, tool: 'run_command', target: 'ls', by: 'grant' }])
    const whole = fold(taskRunUses)
    expect(whole.grants).toEqual([])
    expect(whole.orderedRounds[1]?.allowed).toEqual([{ tool: 'run_command', target: 'ls', by: 'grant' }])
    expect(whole.calls).toEqual([
      { id: 'call-1', tool: 'write_file', target: 'report.txt', state: 'failed' },
      { id: 'call-2', tool: 'run_command', target: 'ls', state: 'done' },
    ])
    expect(whole.activity).toEqual(['[tool] run_command'])
  })

  test('task-resume.jsonl: task_started seeds the earlier turns, the stream’s own round adds to them', () => {
    const state = fold(taskResume)
    expect(state.orderedRounds.map((round) => [round.round, round.outcome])).toEqual([
      [0, 'completed'],
      [1, 'completed'],
      [2, 'completed'],
      [3, 'completed'],
    ])
    expect(state.finished?.status).toBe('done')
    expect(state.view?.plan).toBe('1. Look for the word.\n2. Say it.')
  })

  test('a run the CLI refused before it started: no task, its stderr’s last line the refusal, word for word', () => {
    const state = fold('', 0, JobEnd.of(ChildExit.exited(1), BoundedTail.of(runRefused), false))
    expect(state.taskID).toBeUndefined()
    expect(state.refusal).toBe(runRefused.trim())
    expect(state.activity).toEqual([runRefused.trim()])
  })

  test('one that never started says why, and one its Stop ended is no refusal', () => {
    expect(fold('', 0, JobEnd.notStarted('no recorded answer for apogee task run x')).refusal).toBe('no recorded answer for apogee task run x')
    expect(fold('', 0, JobEnd.of(ChildExit.exited(130), new BoundedTail(), true)).refusal).toBeUndefined()
  })
})
