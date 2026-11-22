// What `/apogee <words>` means: each word of the set planned as the real
// command it runs (the argv pins), the two that change things planned as
// confirmable, never run by the plan; anything else refused with the usage.
import { expect, test } from 'claude-code/testing'

import { planApogee, shownCommand } from '../../../src/business/commands/plan'

test('the reads run the real commands, each with --output-format json', () => {
  expect(planApogee('check')).toEqual({ kind: 'read', read: 'check', args: ['check', '--output-format', 'json'] })
  expect(planApogee('models')).toEqual({ kind: 'read', read: 'models', args: ['models', 'list', '--output-format', 'json'] })
  expect(planApogee('models list')).toEqual(planApogee('models'))
  expect(planApogee('train status')).toEqual({ kind: 'read', read: 'train status', args: ['train', 'status', '--output-format', 'json'] })
  expect(planApogee('  train   status ')).toEqual(planApogee('train status'))
})

test('promote and rollback are planned as confirmable actions with the CLI argv, and no --yes', () => {
  expect(planApogee('promote 20261026-065637 --as tuned')).toEqual({
    kind: 'action',
    action: 'promote',
    args: ['train', 'promote', '20261026-065637', '--as', 'tuned'],
  })
  expect(planApogee('rollback tuned')).toEqual({ kind: 'action', action: 'rollback', args: ['train', 'rollback', 'tuned'] })
  for (const words of ['promote 20261026-065637 --as tuned', 'rollback tuned']) {
    const plan = planApogee(words)
    expect(plan.kind === 'action' && plan.args.some(arg => arg === '--yes' || arg === '-y')).toBe(false)
  }
})

test('nothing, or help, is the overview', () => {
  expect(planApogee('')).toEqual({ kind: 'overview' })
  expect(planApogee('help')).toEqual({ kind: 'overview' })
})

test('words outside the set, or an action missing its words, are refused with the usage and run nothing', () => {
  for (const words of [
    'chat',
    'models pull x',
    'train start',
    'promote',
    'promote 20261026-065637',
    'promote 20261026-065637 tuned',
    'promote 20261026-065637 --as tuned --force',
    'promote 20261026-065637 --as --yes',
    'rollback',
    'rollback tuned extra',
    'rollback --yes',
    'check --fix',
  ]) {
    const plan = planApogee(words)
    expect([words, plan.kind]).toEqual([words, 'refused'])
  }
  expect(planApogee('promote 20261026-065637')).toEqual({
    kind: 'refused',
    reason: 'promote takes a run and the backend: /apogee promote <run> --as <backend>',
  })
  expect(planApogee('chat')).toEqual({ kind: 'refused', reason: "'chat' is not one of /apogee's commands" })
})

test('a run or a backend that reads as a flag is refused, never handed to the CLI', () => {
  expect(planApogee('rollback -x').kind).toBe('refused')
  expect(planApogee('promote --as --as tuned').kind).toBe('refused')
})

test('the command shown before a confirmation is the one that runs, quoted for a shell where it must be', () => {
  expect(shownCommand('/Users/someone/.local/bin/apogee', ['train', 'rollback', 'tuned'])).toBe(
    '/Users/someone/.local/bin/apogee train rollback tuned',
  )
  expect(shownCommand('/Users/some one/bin/apogee', ['train', 'rollback', "it's"])).toBe(
    "'/Users/some one/bin/apogee' train rollback 'it'\\''s'",
  )
})
