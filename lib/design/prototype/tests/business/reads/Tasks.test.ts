import listAll from '@recorded/presentation/tasks/Fixtures/task-list-all.json?raw'
import listInterrupted from '@recorded/presentation/tasks/Fixtures/task-list-interrupted.json?raw'
import listKilled from '@recorded/presentation/tasks/Fixtures/task-list-killed.json?raw'
import listNewest50 from '@recorded/presentation/tasks/Fixtures/task-list-newest-50.json?raw'
import statusExhausted from '@recorded/presentation/tasks/Fixtures/task-status.json?raw'
import statusInterrupted from '@recorded/presentation/tasks/Fixtures/task-status-interrupted.json?raw'
import statusStalledGranted from '@recorded/presentation/tasks/Fixtures/task-status-stalled-granted.json?raw'
import statusUses from '@recorded/presentation/tasks/Fixtures/task-status-uses.json?raw'
import recipes from '@recorded/business/reads/Fixtures/task-recipes.json?raw'
import recipeShow from '@recorded/business/reads/Fixtures/task-recipe-show.json?raw'
import suiteCloud from '@recorded/business/reads/Fixtures/suites-config-get-cloud.txt?raw'
import { describe, expect, test } from 'vitest'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { Suite } from '../../../source/business/reads/SuiteFootprint'
import { TaskRecipe, TaskSummary, TaskView } from '../../../source/business/reads/Tasks'

const document = (text: string) => {
  const read = ReadDocument.from(text)
  if (!read) throw new Error('not a document')
  return read
}

describe("the Tasks place's reads (the Mac's Tasks.swift and Recipes.swift), each over its recording (design 1p)", () => {
  test('task list: each row, the total, and interrupted as the row says it -- undefined where a binary before M32 says nothing', () => {
    const killed = TaskSummary.list(document(listKilled))
    expect(killed).toEqual([
      { id: 'task-20261027-023023-2', goal: 'say hello', status: 'running', roundsUsed: 0, roundsBudget: 2, interrupted: true },
      { id: 'task-20261027-023023', goal: 'answer with the word', status: 'exhausted', roundsUsed: 2, roundsBudget: 2, interrupted: false },
    ])
    expect(killed.map(TaskSummary.isRunning)).toEqual([false, false])
    const older = TaskSummary.list(document(listInterrupted))
    expect(older[0]?.interrupted).toBeUndefined()
    expect(TaskSummary.isRunning(older[0] as TaskSummary)).toBe(true)
    expect([document(listNewest50).total, TaskSummary.list(document(listNewest50)).length, TaskSummary.list(document(listAll)).length]).toEqual([60, 50, 60])
  })

  test('task status: the view, its checks, its turns with their uses, its grants, folder and stamps', () => {
    const view = TaskView.read(document(statusUses).value)
    expect(view).toMatchObject({
      id: 'task-20261109-021211',
      goal: 'Write the report',
      status: 'exhausted',
      plan: '1. Write the report.\n2. List the folder.',
      roundsUsed: 1,
      roundsBudget: 1,
      folder: '/Users/someone/work',
      tools: true,
      grants: ['run_command'],
      createdAt: '2026-11-09T02:12:11Z',
      updatedAt: '2026-11-09T02:12:11Z',
      interrupted: false,
    })
    expect(view?.checks).toEqual([{ kind: 'require_file', value: '/Users/someone/work/report.txt', description: 'the file /Users/someone/work/report.txt exists and is not empty', ran: true, passed: false, detail: 'no such file' }])
    expect(view?.rounds.map((round) => [round.round, round.kind, round.outcome, round.selfReport, round.tools])).toEqual([
      [0, 'plan', 'completed', '', 0],
      [1, 'execute', 'completed', 'done', 1],
    ])
    expect(view?.rounds[1]?.allowed).toEqual([{ tool: 'run_command', target: 'ls', by: 'grant' }])
    expect(view?.rounds[1]?.denied).toEqual([{ tool: 'write_file', target: 'report.txt', by: 'nobody' }])
    expect(view && TaskView.isResumable(view)).toBe(false)
  })

  test('which tasks run now and which resume: running not interrupted runs; interrupted, halted, cancelled, stalled and failed resume; done and exhausted never', () => {
    const interrupted = TaskView.read(document(statusInterrupted).value) as TaskView
    expect([TaskView.isRunning(interrupted), TaskView.isResumable(interrupted)]).toEqual([false, true])
    const stalled = TaskView.read(document(statusStalledGranted).value) as TaskView
    expect([TaskView.isRunning(stalled), TaskView.isResumable(stalled), stalled.grants]).toEqual([false, true, ['write_file']])
    const exhausted = TaskView.read(document(statusExhausted).value) as TaskView
    expect([TaskView.isRunning(exhausted), TaskView.isResumable(exhausted), exhausted.grants, exhausted.tools]).toEqual([false, false, [], false])
    for (const status of ['halted', 'cancelled', 'failed']) expect(TaskView.isResumable({ ...exhausted, status })).toBe(true)
    expect(TaskView.isRunning({ ...exhausted, status: 'planning' })).toBe(true)
  })

  test('a use that is a bare string keeps its tool; another shape names none', () => {
    expect(TaskView.uses(['write_file', { tool: 'run_command', target: 'ls', by: 'grant' }, 3])).toEqual([
      { tool: 'write_file', target: '', by: '' },
      { tool: 'run_command', target: 'ls', by: 'grant' },
    ])
  })

  test('a view needs an id', () => {
    expect(TaskView.read({ goal: 'x' })).toBeUndefined()
  })

  test('task recipes and task recipes show: the list, then one recipe whole with its goal; the goal rendered with the input', () => {
    const listed = TaskRecipe.list(document(recipes))
    expect(listed.map((recipe) => [recipe.name, recipe.allow, recipe.require, recipe.requireFiles, recipe.tools, recipe.rounds, recipe.goal])).toEqual([
      ['capture-decisions', ['run_command', 'write_file'], ['Captured:'], [], true, 8, undefined],
      ['changelog-draft', [], ['## Unreleased'], [], false, 8, undefined],
      ['digest-folder', ['write_file'], [], ['DIGEST.md'], true, 8, undefined],
    ])
    const shown = TaskRecipe.read(document(recipeShow))
    expect(shown?.takesInput).toBe(true)
    expect(shown && TaskRecipe.renderedGoal(shown, 'docs')?.startsWith('Read the folder docs: list it')).toBe(true)
    expect(TaskRecipe.read(document(recipes))).toBeUndefined()
  })

  test('config get suites.<name>: each role and its backend, a pinned window read, a line that is not one kept as said', () => {
    expect(Suite.read('cloud', suiteCloud)).toEqual({ name: 'cloud', members: [{ role: 'chat', backend: 'claude', window: undefined }, { role: 'embedding', backend: 'helper', window: undefined }], unread: [] })
    expect(Suite.read('x', 'chat: root (window 4096)\nhelper\nflavour: sweet\n')).toEqual({ name: 'x', members: [{ role: 'chat', backend: 'root', window: 4096 }], unread: ['helper', 'flavour: sweet'] })
    expect(Suite.windowOf(Suite.read('x', 'chat: root (window 4096)'), 'root')).toBe('4096 pinned')
    expect(Suite.windowOf(Suite.read('cloud', suiteCloud), 'claude')).toBe('its own')
  })
})
