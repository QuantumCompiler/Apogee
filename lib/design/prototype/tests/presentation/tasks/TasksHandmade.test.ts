import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'

/**
 * Design 1p's hand-made files hold to the CLI's shapes: each built from a
 * recording, every line but what the stop or the state changes kept as
 * recorded. Read with Node, as the decode check reads both roots.
 */
const here = dirname(fileURLToPath(import.meta.url))
const prototype = resolve(here, '..', '..', '..')
const recorded = resolve(prototype, '..', '..', 'src', 'darwin', 'tests')
const design = (path: string) => readFileSync(resolve(prototype, 'fixtures', path), 'utf8')
const mac = (path: string) => readFileSync(resolve(recorded, path), 'utf8')
type Line = Record<string, unknown> & { type: string; task?: Record<string, unknown>; transition?: Record<string, unknown>; round?: Record<string, unknown> }
const events = (text: string): Line[] => text.split('\n').filter((line) => line !== '').map((line) => JSON.parse(line) as Line)
const keys = (value: object | undefined) => Object.keys(value ?? {}).sort()

describe("the hand-made task and suite files are the CLI's shapes (design 1p)", () => {
  test('each ledger is task list’s shape, every row a recorded task status’s own, newest first', () => {
    const recordedList = JSON.parse(mac('presentation/tasks/Fixtures/task-list-killed.json')) as { data: object[] }
    const status = (file: string) => JSON.parse(mac(`presentation/tasks/Fixtures/${file}`)) as Record<string, unknown>
    const rowOf = (view: Record<string, unknown>) => ({ goal: view.goal, id: view.id, interrupted: view.interrupted, rounds_budget: view.rounds_budget, rounds_used: view.rounds_used, status: view.status })
    const lists = {
      'task-list-handmade.json': ['task-status-halted-done.json', 'task-status-exhausted.json', 'task-status-interrupted.json'],
      'task-list-stalled-handmade.json': ['task-status-halted-done.json', 'task-status-stalled.json', 'task-status-exhausted.json', 'task-status-interrupted.json'],
      'task-list-stalled-granted-handmade.json': ['task-status-halted-done.json', 'task-status-stalled-granted.json', 'task-status-exhausted.json', 'task-status-interrupted.json'],
    }
    for (const [file, statuses] of Object.entries(lists)) {
      const list = JSON.parse(design(`presentation/tasks/${file}`)) as { data: object[]; object: string; total: number }
      expect(keys(list), file).toEqual(['data', 'object', 'total'])
      expect(list.data.map((each) => keys(each)), file).toEqual(list.data.map(() => keys(recordedList.data[0])))
      expect(list.data, file).toEqual(statuses.map((name) => rowOf(status(name))))
      expect(list.total, file).toBe(statuses.length)
    }
  })

  test('the live run stopped: its endings are task-run.jsonl’s own lines’ shapes, halted after the plan or a round, cancelled during the plan or a round', () => {
    const run = events(mac('presentation/tasks/Fixtures/task-run.jsonl'))
    const finished = run.at(-1) as Line
    const haltedRecorded = events(mac('presentation/tasks/Fixtures/task-run-halted.jsonl')).at(-1) as Line
    const endings = {
      'task-run-halt-after-plan-handmade.jsonl': { status: 'halted', reason: "halted by 'apogee task halt' after the plan", turns: ['plan:completed'], lines: ['task_finished'] },
      'task-run-halt-after-round-1-handmade.jsonl': { status: 'halted', reason: "halted by 'apogee task halt' after round 1", turns: ['plan:completed', 'execute:completed'], lines: ['task_finished'] },
      'task-run-cancel-plan-handmade.jsonl': { status: 'cancelled', reason: "cancelled by 'apogee task cancel' during the plan turn", turns: ['plan:interrupted'], lines: ['error', 'task_finished'] },
      'task-run-cancel-round-1-handmade.jsonl': { status: 'cancelled', reason: "cancelled by 'apogee task cancel' during round 1", turns: ['plan:completed', 'execute:interrupted'], lines: ['error', 'task_round', 'task_finished'] },
      'task-run-cancel-round-2-handmade.jsonl': { status: 'cancelled', reason: "cancelled by 'apogee task cancel' during round 2", turns: ['plan:completed', 'execute:completed', 'correct:interrupted'], lines: ['error', 'task_round', 'task_finished'] },
    }
    for (const [file, expected] of Object.entries(endings)) {
      const lines = events(design(`presentation/tasks/${file}`))
      expect(lines.map((line) => line.type), file).toEqual(expected.lines)
      const end = lines.at(-1) as Line
      expect(keys(end), file).toEqual(keys(finished))
      expect(keys(end.task), file).toEqual(keys(finished.task))
      expect(keys(end), file).toEqual(keys(haltedRecorded))
      expect([end.status, end.reason, end.task?.status, end.task?.reason, end.task?.id, end.task?.goal, end.transition], file).toEqual([
        expected.status,
        expected.reason,
        expected.status,
        expected.reason,
        'task-20261021-011456',
        'say hello',
        { at: '2026-10-21T01:14:56Z', detail: expected.reason, event: 'finished', status: expected.status },
      ])
      expect((end.task?.turns as Array<{ kind: string; outcome: string }>).map((turn) => `${turn.kind}:${turn.outcome}`), file).toEqual(expected.turns)
      if (lines[0]?.type === 'error') expect(lines[0]).toEqual({ message: 'cancelled', type: 'error' })
      const cut = lines.find((line) => line.type === 'task_round')
      if (cut) {
        const recordedEnd = run.find((line) => line.type === 'task_round' && line.transition?.event === 'round_ended') as Line
        expect(keys(cut), file).toEqual(keys(recordedEnd))
        expect([cut.transition?.event, cut.transition?.detail, cut.transition?.status, cut.round?.outcome], file).toEqual(['round_ended', 'interrupted', 'cancelled', 'interrupted'])
      }
    }
  })

  test('the resume stopped: halted or cancelled before its round, or cancelled during it, each task-resume.jsonl’s shapes', () => {
    const resume = events(mac('presentation/tasks/Fixtures/task-resume.jsonl'))
    const finished = resume.at(-1) as Line
    const endings = {
      'task-resume-halt-starting-handmade.jsonl': ['halted', "halted by 'apogee task halt' before round 3"],
      'task-resume-cancel-starting-handmade.jsonl': ['cancelled', "cancelled by 'apogee task cancel' before round 3"],
      'task-resume-cancel-round-3-handmade.jsonl': ['cancelled', "cancelled by 'apogee task cancel' during round 3"],
    }
    for (const [file, [status, reason]] of Object.entries(endings)) {
      const end = events(design(`presentation/tasks/${file}`)).at(-1) as Line
      expect(keys(end), file).toEqual(keys(finished))
      expect(keys(end.task), file).toEqual(keys(finished.task))
      expect([end.status, end.reason, end.task?.id, end.task?.rounds_used], file).toEqual([status, reason, 'task-20261109-021224', 2])
    }
  })

  test('task halt and task cancel answer in task_cmd.cpp’s words', () => {
    expect(design('presentation/tasks/task-halt-011456-handmade.txt')).toBe('asked task task-20261021-011456 to halt: it stops when its round ends\n')
    expect(design('presentation/tasks/task-cancel-021224-handmade.txt')).toBe("asked task task-20261109-021224 to cancel: its turn ends now, through the loop's own cancellation\n")
  })

  test('the two hand-made recipe shows are task-recipes.json’s rows with show’s keys, their goals the old recipes()’ words', () => {
    const recordedShow = JSON.parse(mac('business/reads/Fixtures/task-recipe-show.json')) as object
    const rows = (JSON.parse(mac('business/reads/Fixtures/task-recipes.json')) as { data: Array<Record<string, unknown>> }).data
    for (const name of ['capture-decisions', 'changelog-draft']) {
      const show = JSON.parse(design(`business/reads/task-recipe-show-${name}-handmade.json`)) as Record<string, unknown>
      expect(keys(show), name).toEqual(keys(recordedShow))
      const { goal, object, ...rest } = show
      expect(rest, name).toEqual(rows.find((row) => row.name === name))
      expect([object, typeof goal, (goal as string).includes('{{input}}')], name).toEqual(['task.recipe', 'string', true])
    }
  })

  test('config set-default-suite answers with point_default_suite’s line for each recorded suite, and (none) for off', () => {
    for (const name of mac('business/reads/Fixtures/suites-config-get.txt').split(/\s+/).filter(Boolean)) expect(design(`business/reads/config-set-default-suite-${name}-handmade.txt`)).toBe(`models.default_suite = ${name}\n`)
    expect(design('business/reads/config-set-default-suite-off-handmade.txt')).toBe('models.default_suite = (none)\n')
  })

  test('the execute preset’s coding: config get suites.<name>’s shape (suites-config-get-shared.txt), its members those its hand-made footprint names in their roles', () => {
    const footprint = JSON.parse(design('business/reads/models-status-coding-handmade.json')) as { fields: Array<{ field: string; value: string }> }
    const members = (footprint.fields.find((field) => field.field === 'footprint')?.value ?? '').split('\n').slice(1, -1).map((line) => /^(\S+) \(([^)]+)\):/.exec(line)?.slice(1, 3))
    expect(design('business/reads/config-get-suites-coding-handmade.txt')).toBe(members.map((member) => `${member?.[1]}: ${member?.[0]}`).join('\n') + '\n')
    expect(/^(\w+: \S+\n)+$/.test(mac('business/reads/Fixtures/suites-config-get-shared.txt'))).toBe(true)
  })
})
