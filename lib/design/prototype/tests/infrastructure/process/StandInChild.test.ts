import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'
import chatsShow from '@recorded/business/reads/Fixtures/chats-show.json?raw'
import chatsShowAfterStop from '@recorded/business/reads/Fixtures/chats-show-after-stop.json?raw'
import launchRefusedModel from '@recorded/data/protocol/Fixtures/launch-refused-model.err?raw'
import launchRefusedModelExit from '@recorded/data/protocol/Fixtures/launch-refused-model.exit?raw'
import storeList from '@recorded/business/reads/Fixtures/store-list.json?raw'
import storeListAll from '@recorded/business/reads/Fixtures/store-list-all.json?raw'
import storeListKinds from '@recorded/business/reads/Fixtures/store-list-kinds.json?raw'
import storeListRoutes from '@recorded/business/reads/Fixtures/store-list-routes.json?raw'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'
import { recordedExit, StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ModeRules, Rules } from '../../../source/infrastructure/process/StandInChild'

const modes: ModeRules = { configTypoRefusal: agentsListBadConfig }

function child(rules: Rules, apogee: 'installed' | 'missing' | 'config-typo' = 'installed'): StandInChild {
  return new StandInChild({ rules, apogee }, modes)
}

/** What a call printed, when it started. */
function printed(answer: ReturnType<StandInChild['run']>): { stdout: string; stderr: string; exit: string; timedOut: boolean } {
  if (!answer.started) throw new Error(`it did not start: ${answer.reason}`)
  const { run } = answer
  return { stdout: run.stdout, stderr: run.stderrTail.text, exit: ChildExit.description(run.exit), timedOut: run.timedOut }
}

describe('the stand-in child: its rule table', () => {
  test('the longest prefix wins', () => {
    const stand = child({
      reads: [
        { arguments: ['store', 'list'], answer: { stdout: storeList } },
        { arguments: ['store', 'list', '--all'], answer: { stdout: storeListAll } },
        { arguments: ['store'], answer: { stdout: storeListKinds } },
      ],
    })
    expect(printed(stand.run('reads', ['store', 'list', '--all', '--kinds'])).stdout).toBe(storeListAll)
    expect(printed(stand.run('reads', ['store', 'list'])).stdout).toBe(storeList)
    expect(printed(stand.run('reads', ['store', 'info'])).stdout).toBe(storeListKinds)
  })

  test('an exact rule answers its arguments whole and no longer line (design 1h): `models quantize <h>` is not `models quantize <h> -t Q8_0`', () => {
    const stand = child({ jobs: [{ arguments: ['models', 'quantize', 'x/gguf/1'], exact: true, answer: { stdout: storeList } }] })
    expect(printed(stand.run('jobs', ['models', 'quantize', 'x/gguf/1'])).stdout).toBe(storeList)
    expect(stand.run('jobs', ['models', 'quantize', 'x/gguf/1', '-t', 'Q8_0'])).toEqual({ started: false, reason: 'no recorded answer for apogee models quantize x/gguf/1 -t Q8_0' })
    expect(stand.unanswered).toEqual(['jobs: apogee models quantize x/gguf/1 -t Q8_0'])
  })

  test('of two equally long prefixes, the later rule wins (PreviewStore\'s rule)', () => {
    const stand = child({
      reads: [
        { arguments: ['store', 'list'], answer: { stdout: storeList } },
        { arguments: ['store', 'list'], answer: { stdout: storeListRoutes } },
      ],
    })
    expect(printed(stand.run('reads', ['store', 'list', '--all'])).stdout).toBe(storeListRoutes)
  })

  test('a rule\'s answers come in order, one a call, and the last repeats', () => {
    const stand = child({
      reads: [{ arguments: ['chats', 'show'], answer: [{ stdout: chatsShow }, { stdout: chatsShowAfterStop }] }],
    })
    expect([1, 2, 3, 4].map(() => printed(stand.run('reads', ['chats', 'show', 'wal'])).stdout)).toEqual([
      chatsShow,
      chatsShowAfterStop,
      chatsShowAfterStop,
      chatsShowAfterStop,
    ])
  })

  test('a rule that waits on another call (design 1k): refused, uncounted, until that call is answered with a success; then its own answers, in order', () => {
    const refused = { stderr: 'apogee video generate: a LoRA loads through peft -- apogee train setup --with video', exit: 1 }
    const stand = child({
      jobs: [
        { arguments: ['video', 'generate'], until: { ran: ['train', 'setup', '--with', 'video'], answer: refused }, answer: [{ stdout: storeList }, { stdout: storeListAll }] },
        { arguments: ['train', 'setup', '--with', 'video'], answer: [{ stderr: 'pip failed', exit: 1 }, { stdout: 'installed' }] },
      ],
    })
    const generate = () => printed(stand.run('jobs', ['video', 'generate', '-o', 'x.mp4']))
    // However often it is pressed, nothing went in, so the CLI refuses.
    expect([generate(), generate()].map((run) => run.exit)).toEqual(['exited with status 1', 'exited with status 1'])
    // An install that failed is no success.
    expect(printed(stand.run('jobs', ['train', 'setup', '--with', 'video'])).exit).toBe('exited with status 1')
    expect(generate()).toEqual({ stdout: '', stderr: refused.stderr, exit: 'exited with status 1', timedOut: false })
    // One that went in: the rule's own answers play, from its first, as if never called before.
    expect(printed(stand.run('jobs', ['train', 'setup', '--with', 'video'])).stdout).toBe('installed')
    expect([generate(), generate(), generate()].map((run) => run.stdout)).toEqual([storeList, storeListAll, storeListAll])
  })

  test('a rule refused while a file holds a text (design 1n): its refusal, uncounted, while the stand-in’s folder file holds it; its own answers once it does not', () => {
    const refusal = { stderr: 'unknown key', exit: 1 }
    const stand = new StandInChild(
      {
        rules: { reads: [{ arguments: ['symphonies', 'show', 'notes'], refusedWhile: { folder: '/data/symphonies', name: 'notes.yaml', holds: 'temprature:', answer: refusal }, answer: [{ stdout: 'first' }, { stdout: 'second' }, { stdout: 'third' }] }] },
        apogee: 'installed',
        folders: { '/data/symphonies': [{ name: 'notes.yaml', text: 'role: chat\n' }] },
      },
      modes,
    )
    const show = () => printed(stand.run('reads', ['symphonies', 'show', 'notes']))
    expect(show().stdout).toBe('first')
    stand.folders.write('/data/symphonies', { name: 'notes.yaml', text: 'role: chat\ntemprature: 0.2\n' })
    for (let call = 0; call < 2; call += 1) expect(show(), 'refused as often as it is asked').toEqual({ stdout: '', stderr: 'unknown key', exit: 'exited with status 1', timedOut: false })
    stand.folders.write('/data/symphonies', { name: 'notes.yaml', text: 'role: chat\n' })
    expect(show().stdout, 'the refusals were not counted: its second answer, not its third').toBe('second')
  })

  test('a call stopped before its end (its Stop, design 1k) did not succeed: a rule waiting on it waits again', () => {
    const stand = child({
      jobs: [
        { arguments: ['video', 'generate'], until: { ran: ['train', 'setup'], answer: { exit: 1 } }, answer: { stdout: storeList } },
        { arguments: ['train', 'setup'], answer: { stdout: 'installed' } },
      ],
    })
    stand.run('jobs', ['train', 'setup'])
    stand.stopped('jobs', ['train', 'setup'])
    expect(printed(stand.run('jobs', ['video', 'generate'])).exit).toBe('exited with status 1')
    stand.run('jobs', ['train', 'setup'])
    expect(printed(stand.run('jobs', ['video', 'generate'])).stdout).toBe(storeList)
  })

  test('an answer’s replay rules: where its end is held (design 1k’s `holdAt`), all its lines unless it names a line count', () => {
    const stand = child({ jobs: [{ arguments: ['train', 'setup'], answer: [{ stdout: 'a\nb\nc\n', exitAfter: 3, holdAt: 1 }, { stdout: 'a\n' }] }] })
    const first = stand.run('jobs', ['train', 'setup'])
    const second = stand.run('jobs', ['train', 'setup'])
    expect(first.started && first.replay).toMatchObject({ exitAfter: 3, holdAt: 1 })
    expect(second.started && second.replay.holdAt).toBeUndefined()
  })

  test('it answers with what the CLI printed: stdout, stderr and an exit, or a timeout', () => {
    const stand = child({
      chats: [{ arguments: ['chat'], answer: { stderr: launchRefusedModel, exit: recordedExit(launchRefusedModelExit) } }],
      commands: [{ arguments: ['models', 'pull'], answer: { timedOut: true } }],
    })
    expect(printed(stand.run('chats', ['chat', '-m', 'nothing']))).toEqual({
      stdout: '',
      stderr: launchRefusedModel,
      exit: 'exited with status 1',
      timedOut: false,
    })
    expect(printed(stand.run('commands', ['models', 'pull', 'x']))).toEqual({
      stdout: '',
      stderr: '',
      exit: 'ended by signal 15',
      timedOut: true,
    })
  })

  test('reads, commands, jobs and chats are four tables: a read\'s rule never answers a command', () => {
    const stand = child({ reads: [{ arguments: ['agents', 'list'], answer: { stdout: agentsList } }] })
    expect(stand.run('commands', ['agents', 'list'])).toEqual({
      started: false,
      reason: 'no recorded answer for apogee agents list',
    })
  })

  test('a call nobody answered does not start, and is logged; every call is kept, with its input', () => {
    const stand = child({ commands: [{ arguments: ['auth', 'add'], answer: {} }] })
    expect(stand.run('reads', ['system'])).toEqual({ started: false, reason: 'no recorded answer for apogee system' })
    stand.run('commands', ['auth', 'add', 'anthropic'], 'the key, on stdin')
    expect(stand.unanswered).toEqual(['reads: apogee system'])
    expect(stand.calls).toEqual([
      { kind: 'reads', arguments: ['system'] },
      { kind: 'commands', arguments: ['auth', 'add', 'anthropic'], input: 'the key, on stdin' },
    ])
  })

  test('a recorded .exit is read as its status, and anything else is refused', () => {
    expect(recordedExit(launchRefusedModelExit)).toBe(1)
    expect(() => recordedExit('one\n')).toThrow('a recorded .exit holds one status, not "one\\n"')
  })
})

describe('the stand-in child: its three modes', () => {
  const rules: Rules = { reads: [{ arguments: ['agents', 'list'], answer: { stdout: agentsList } }] }

  test('installed: the binary is found where the previews say, and the rules answer', () => {
    const stand = child(rules)
    expect(stand.locateBinary()).toEqual({ case: 'found', binary: '/Users/someone/.local/bin/apogee' })
    expect(printed(stand.run('reads', ['agents', 'list'])).stdout).toBe(agentsList)
  })

  test('missing: no binary is found, and nothing runs', () => {
    const stand = child(rules, 'missing')
    expect(stand.locateBinary()).toEqual({ case: 'notFound' })
    expect(stand.run('reads', ['agents', 'list'])).toEqual({
      started: false,
      reason: 'apogee is not installed',
    })
    expect(stand.calls).toEqual([])
    expect(stand.unanswered).toEqual([])
  })

  test('config-typo: every call the CLI reads its config for exits 1 with the recorded refusal, its verb swapped', () => {
    const stand = child(rules, 'config-typo')
    expect(stand.locateBinary()).toEqual({ case: 'found', binary: '/Users/someone/.local/bin/apogee' })
    for (const [kind, args] of [
      ['reads', ['agents', 'list']],
      ['reads', ['review', 'list']],
      ['jobs', ['models', 'pull', 'x']],
    ] as const) {
      expect(printed(stand.run(kind, args))).toEqual({
        stdout: '',
        stderr: agentsListBadConfig.replace('apogee agents:', `apogee ${args[0]}:`),
        exit: 'exited with status 1',
        timedOut: false,
      })
    }
    expect(stand.calls.map((call) => call.arguments[0])).toEqual(['agents', 'review', 'models'])
  })

  test('config-typo: what the CLI answers without reading its config -- `review skills list` and `show` -- is answered by its rules; `review targets` is still refused', () => {
    const skills = '{"object":"list","data":[]}\n'
    const security = '{"object":"review.skill","name":"security"}\n'
    const stand = child(
      {
        reads: [
          { arguments: ['review', 'skills', 'list'], answer: { stdout: skills } },
          { arguments: ['review', 'skills', 'show', 'security'], answer: { stdout: security } },
          { arguments: ['review', 'targets'], answer: { stdout: '{}' } },
        ],
      },
      'config-typo',
    )
    expect(printed(stand.run('reads', ['review', 'skills', 'list']))).toEqual({ stdout: skills, stderr: '', exit: 'exited with status 0', timedOut: false })
    expect(printed(stand.run('reads', ['review', 'skills', 'show', 'security']))).toEqual({ stdout: security, stderr: '', exit: 'exited with status 0', timedOut: false })
    expect(printed(stand.run('reads', ['review', 'targets']))).toEqual({ stdout: '', stderr: agentsListBadConfig.replace('apogee agents:', 'apogee review:'), exit: 'exited with status 1', timedOut: false })
    expect(stand.run('reads', ['review', 'skills', 'show', 'invented']), 'a config-free call nobody recorded is unanswered, not refused').toEqual({ started: false, reason: 'no recorded answer for apogee review skills show invented' })
    expect(stand.unanswered).toEqual(['reads: apogee review skills show invented'])
    expect(StandInChild.configFree).toEqual([['review', 'skills']])
  })
})

describe('what a child leaves', () => {
  test('an exit says itself as the Mac app says it', () => {
    expect(ChildExit.description(ChildExit.exited(0))).toBe('exited with status 0')
    expect(ChildExit.succeeded(ChildExit.exited(0))).toBe(true)
    expect(ChildExit.succeeded(ChildExit.exited(1))).toBe(false)
    expect(ChildExit.succeeded({ reason: 'signalled', status: 0 })).toBe(false)
    expect(ChildExit.description({ reason: 'signalled', status: 2 })).toBe('ended by signal 2')
  })

  test('a stderr tail keeps the last 8 KiB, and its last line is the last with anything on it, trimmed', () => {
    const tail = BoundedTail.of('first\n  the reason  \n\n')
    expect(tail.lastLine).toBe('the reason')
    expect(tail.isEmpty).toBe(false)
    expect(new BoundedTail().lastLine).toBeUndefined()
    expect(new BoundedTail().isEmpty).toBe(true)
    const long = new BoundedTail()
    long.append('x'.repeat(9000))
    long.append('\nend')
    expect(long.text.length).toBe(8 * 1024)
    expect(long.text.endsWith('\nend')).toBe(true)
  })
})
