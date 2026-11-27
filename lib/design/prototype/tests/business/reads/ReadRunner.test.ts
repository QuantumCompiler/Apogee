import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'
import checkAdapters from '@recorded/business/reads/Fixtures/check-adapters.json?raw'
import chatsShowRefused from '@recorded/business/reads/Fixtures/chats-show-refused.err?raw'
import chatsShowOlder from '@recorded/business/reads/Fixtures/chats-show-warnings-older.json?raw'
import chatsShowWarnings from '@recorded/business/reads/Fixtures/chats-show-warnings-older.err?raw'
import configScan from '@recorded/business/reads/Fixtures/config-scan.txt?raw'
import { CommandOutcome } from '../../../source/business/reads/CommandRunning'
import type { CommandRunning } from '../../../source/business/reads/CommandRunning'
import { ReadOutcome, ReadRunner } from '../../../source/business/reads/ReadRunner'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'

const binary = StandInChild.binary

function runner(rules: Rules, apogee: ApogeeMode = 'installed'): { runner: CommandRunning; child: StandInChild } {
  const child = new StandInChild({ rules, apogee }, { configTypoRefusal: agentsListBadConfig })
  return { runner: new ReadRunner(child), child }
}

describe('a read, and its five outcomes, each from a rule', () => {
  const { runner: reads } = runner({
    reads: [
      { arguments: ['agents', 'list'], answer: { stdout: agentsList } },
      { arguments: ['chats', 'show'], answer: { stderr: chatsShowRefused, exit: 106 } },
      { arguments: ['config', 'scan'], answer: { stdout: configScan } },
      { arguments: ['models', 'list', '--all'], answer: { timedOut: true } },
    ],
  })

  test('.document: agents list answered by agents-list.json is the document the Swift tests decode', async () => {
    const outcome = await reads.read(binary, ['agents', 'list'])
    expect(outcome).toEqual(ReadOutcome.document(ReadDocument.from(agentsList) as ReadDocument))
    expect(outcome.case === 'document' && outcome.document.value).toEqual(JSON.parse(agentsList))
    expect(ReadOutcome.failure(outcome)).toBeUndefined()
  })

  test('.failed: the exit and the CLI\'s reason', async () => {
    const outcome = await reads.read(binary, ['chats', 'show', 'wal'])
    expect(outcome).toEqual(ReadOutcome.failed(ChildExit.exited(106), BoundedTail.of(chatsShowRefused)))
    expect(ReadOutcome.failure(outcome)).toBe(`${BoundedTail.of(chatsShowRefused).lastLine} (exited with status 106)`)
  })

  test('.undecodable: what it printed is not one JSON document, its first 4 KiB kept', async () => {
    const outcome = await reads.read(binary, ['config', 'scan'])
    expect(outcome).toEqual(ReadOutcome.undecodable(configScan.slice(0, 4096), new BoundedTail()))
    expect(ReadOutcome.failure(outcome)).toBe('apogee printed something that is not one JSON document')
  })

  test('.timedOut: after the runner\'s time, which is 60 seconds', async () => {
    const outcome = await reads.read(binary, ['models', 'list', '--all'])
    expect(outcome).toEqual(ReadOutcome.timedOut(60_000, new BoundedTail()))
    expect(ReadOutcome.failure(outcome)).toBe('it did not finish within 60.0 seconds')
  })

  test('.couldNotStart: a read nobody answered, naming its arguments, and logged', async () => {
    const { runner: unanswered, child } = runner({})
    const outcome = await unanswered.read(binary, ['system'])
    expect(outcome).toEqual(ReadOutcome.couldNotStart('no recorded answer for apogee system'))
    expect(ReadOutcome.failure(outcome)).toBe('no recorded answer for apogee system')
    expect(child.unanswered).toEqual(['reads: apogee system'])
  })
})

describe('a command, and its four outcomes', () => {
  const { runner: commands, child } = runner({
    commands: [
      { arguments: ['config', 'scan'], answer: { stdout: configScan } },
      { arguments: ['chats', 'show'], answer: { stderr: chatsShowRefused, exit: 106 } },
      { arguments: ['models', 'pull'], answer: { timedOut: true } },
      { arguments: ['auth', 'add'], answer: {} },
    ],
  })

  test('.done, .failed, .timedOut and .couldNotStart, each from a rule or none', async () => {
    expect(await commands.command(binary, ['config', 'scan'])).toEqual(CommandOutcome.done(configScan))
    const failed = await commands.command(binary, ['chats', 'show', 'wal'])
    expect(failed).toEqual(CommandOutcome.failed(ChildExit.exited(106), BoundedTail.of(chatsShowRefused)))
    expect(CommandOutcome.failure(failed)).toBe(`${BoundedTail.of(chatsShowRefused).lastLine} (exited with status 106)`)
    expect(await commands.command(binary, ['models', 'pull', 'x'])).toEqual(CommandOutcome.timedOut(60_000, new BoundedTail()))
    const none = await commands.command(binary, ['reset'])
    expect(none).toEqual(CommandOutcome.couldNotStart('no recorded answer for apogee reset'))
    expect(CommandOutcome.succeeded(none)).toBe(false)
    expect(CommandOutcome.succeeded(CommandOutcome.done(''))).toBe(true)
  })

  test('command(input:) records the input it was given: a secret goes on stdin, never the command line', async () => {
    expect(await commands.command(binary, ['auth', 'add', 'anthropic'], 'the key')).toEqual(CommandOutcome.done(''))
    expect(child.calls.at(-1)).toEqual({ kind: 'commands', arguments: ['auth', 'add', 'anthropic'], input: 'the key' })
  })
})

describe('report and readKeepingStderr, as Swift\'s', () => {
  test('report keeps a document on a non-zero exit: check exits 1 when a row fails, and its document says which', async () => {
    const { runner: reads } = runner({ reads: [{ arguments: ['check'], answer: { stdout: checkAdapters, exit: 1 } }] })
    expect(await reads.report(binary, ['check'])).toEqual(ReadOutcome.document(ReadDocument.from(checkAdapters) as ReadDocument))
    expect(await reads.read(binary, ['check'])).toEqual(ReadOutcome.failed(ChildExit.exited(1), new BoundedTail()))
  })

  test('report with no document: timed out, undecodable on a success, failed otherwise', async () => {
    const { runner: reads } = runner({
      reads: [
        { arguments: ['a'], answer: { timedOut: true } },
        { arguments: ['b'], answer: { stdout: configScan } },
        { arguments: ['c'], answer: { stderr: chatsShowRefused, exit: 106 } },
      ],
    })
    expect((await reads.report(binary, ['a'])).case).toBe('timedOut')
    expect((await reads.report(binary, ['b'])).case).toBe('undecodable')
    expect((await reads.report(binary, ['c'])).case).toBe('failed')
  })

  test('readKeepingStderr returns the recorded .err beside the document', async () => {
    const { runner: reads } = runner({ reads: [{ arguments: ['chats', 'show'], answer: { stdout: chatsShowOlder, stderr: chatsShowWarnings } }] })
    const read = await reads.readKeepingStderr(binary, ['chats', 'show', 'wal'])
    expect(read.outcome).toEqual(ReadOutcome.document(ReadDocument.from(chatsShowOlder) as ReadDocument))
    expect(read.stderrTail.text).toBe(chatsShowWarnings)
    expect(read.stderrLines).toEqual(
      chatsShowWarnings
        .split('\n')
        .map((line) => line.trim())
        .filter((line) => line !== ''),
    )
  })
})

describe('the modes, through a read', () => {
  test('in config-typo, agents list returns the CLI\'s refusal, word for word, as the old prototype says it', async () => {
    const { runner: reads } = runner({ reads: [{ arguments: ['agents', 'list'], answer: { stdout: agentsList } }] }, 'config-typo')
    const outcome = await reads.read(binary, ['agents', 'list'])
    expect(outcome).toEqual(ReadOutcome.failed(ChildExit.exited(1), BoundedTail.of(agentsListBadConfig)))
    // The old prototype's cfgRefusal('agents') (main.html:4820 at b375fac9).
    expect(ReadOutcome.failure(outcome)).toBe(
      "apogee agents: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)",
    )
  })

  test('in missing, a read could not start, and nothing ran', async () => {
    const { runner: reads, child } = runner({ reads: [{ arguments: ['agents', 'list'], answer: { stdout: agentsList } }] }, 'missing')
    expect(await reads.read(binary, ['agents', 'list'])).toEqual(ReadOutcome.couldNotStart('apogee is not installed'))
    expect(child.calls).toEqual([])
  })
})
