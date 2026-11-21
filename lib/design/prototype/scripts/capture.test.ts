import { chmodSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, realpathSync, rmSync, statSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterEach, describe, expect, test } from 'vitest'
import { branchBinary, capture, captureCommand, CaptureRefusal, commandRecipes, minimalPdf, recipes } from './capture.ts'
import type { CommandRecipe, Recipe } from './capture.ts'

/** The prototype's root: this file is scripts/capture.test.ts. */
const prototype = resolve(dirname(fileURLToPath(import.meta.url)), '..')

const made: string[] = []
afterEach(() => {
  for (const path of made.splice(0)) rmSync(path, { recursive: true, force: true })
})

/** A scratch folder of the test's own, removed after it. */
function scratch(): string {
  const path = mkdtempSync(join(tmpdir(), 'apogee-capture-test-'))
  made.push(path)
  return path
}

/** Every file under `root`, relative to it. */
function filesUnder(root: string): string[] {
  if (!existsSync(root)) return []
  return readdirSync(root).flatMap((name) => {
    const path = join(root, name)
    return statSync(path).isDirectory() ? filesUnder(path).map((inner) => join(name, inner)) : [name]
  })
}

/**
 * A fake `apogee`: it logs each call's arguments and where its home was, says
 * a session line and, for each `user` line, a turn; a `chats show` prints a
 * document. It never touches anything but its log.
 */
function fakeBinary(folder: string): { binary: string; log: string; stdin: string } {
  const log = join(folder, 'calls.log')
  const stdin = join(folder, 'stdin.log')
  const binary = join(folder, 'apogee')
  writeFileSync(
    binary,
    `#!/usr/bin/env node
const fs = require('node:fs')
const actors = Object.fromEntries(fs.readdirSync(process.env.APOGEE_HOME).filter((name) => name.endsWith('.json')).map((name) => [name, fs.readFileSync(process.env.APOGEE_HOME + '/' + name, 'utf8')]))
const seeded = fs.existsSync('src/ingest/runner.py') ? fs.readFileSync('src/ingest/runner.py', 'utf8') : null
fs.appendFileSync(${JSON.stringify(log)}, JSON.stringify({ args: process.argv.slice(2), home: process.env.APOGEE_HOME, userHome: process.env.HOME, cwd: process.cwd(), actors, seeded }) + '\\n')
const args = process.argv.slice(2)
if (args[0] === 'chats') { process.stdout.write('{"object":"chat","id":"20261119-000000-fake","turns":[]}\\n'); process.exit(0) }
if (args.includes('stream-json')) {
  const resumed = args.includes('--resume')
  process.stdout.write(JSON.stringify({ model: 'root', next_turn: resumed ? 2 : 1, protocol_version: 1, chat_id: '20261119-000000-fake', type: 'session' }) + '\\n')
  let turn = resumed ? 1 : 0
  let rest = ''
  process.stdin.on('data', (chunk) => {
    rest += chunk
    let at
    while ((at = rest.indexOf('\\n')) >= 0) {
      const text = rest.slice(0, at)
      const line = JSON.parse(text); rest = rest.slice(at + 1)
      fs.appendFileSync(${JSON.stringify(stdin)}, text + '\\n')
      if (line.type === 'user' && line.text.startsWith('ask')) { turn += 1; process.stdout.write(JSON.stringify({ questions: [{ header: 'Name', question: 'Which?', options: [] }], turn, type: 'question' }) + '\\n') }
      else if (line.type === 'user') { turn += 1; process.stdout.write(JSON.stringify({ finish_reason: 'stop', model: 'root', text: 'ROOT<' + line.text + '>', turn, type: 'result' }) + '\\n') }
      if (line.type === 'answer') process.stdout.write(JSON.stringify({ finish_reason: 'stop', model: 'root', text: 'ANSWERED<' + line.text + '>', turn, type: 'result' }) + '\\n')
      if (line.type === 'cancel') process.stdout.write(JSON.stringify({ finish_reason: 'cancelled', model: 'root', text: '', turn, type: 'result' }) + '\\n')
    }
  })
  process.stdin.on('end', () => process.exit(0))
} else process.exit(0)
`,
  )
  chmodSync(binary, 0o755)
  return { binary, log, stdin }
}

const recipe: Recipe = {
  name: 'fake-play',
  output: 'data/protocol/fake-play.jsonl',
  folder: { 'AGENTS.md': 'Notes.\n', 'minutes.py': 'def f(t):\n    return t\n' },
  backends: [{ name: 'root', actor: 'play-root.json' }],
  suites: [{ name: 'coding', chat: 'root', utility: 'root', orchestrate: 'on' }],
  defaultBackend: 'root',
  launch: ['execute', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools', '--suite', 'coding'],
  turns: ['/play summarize-verify hello'],
  chatsShow: 'business/reads/fake-play-chats-show.json',
}

describe('the capture script: a stream recorded from the branch build, in a throwaway home (design 1u)', () => {
  test('it refuses a binary that is not there, and writes nothing', async () => {
    const fixtures = scratch()
    await expect(capture(recipe, { binary: join(fixtures, 'no-such-apogee'), fixturesRoot: fixtures, tempRoot: scratch() })).rejects.toThrow(CaptureRefusal)
    await expect(capture(recipe, { binary: join(fixtures, 'no-such-apogee'), fixturesRoot: fixtures, tempRoot: scratch() })).rejects.toThrow(/no executable apogee at .*no-such-apogee/)
    expect(filesUnder(fixtures)).toEqual([])
  })

  test("its default is the branch build, and it never names the person's installs or homes", () => {
    expect(relative(prototype, branchBinary(prototype))).toBe(join('..', '..', 'src', 'cli', 'build', 'macos-arm64', 'source', 'apogee'))
    const source = readFileSync(join(prototype, 'scripts', 'capture.ts'), 'utf8')
    expect(source).not.toContain('.local/bin')
    expect(source).not.toMatch(/\.apogee(-dev)?\b/)
    expect(source).not.toMatch(/homedir\(/)
  })

  test('it runs the binary in a throwaway home and working folder, writes only the stream and the chats show document under fixtures/, and leaves no temp folder', async () => {
    const fixtures = scratch()
    const temp = scratch()
    const fake = fakeBinary(scratch())
    const written = await capture(recipe, { binary: fake.binary, fixturesRoot: fixtures, tempRoot: temp })
    expect(written.map((path) => relative(fixtures, path)).sort()).toEqual(['business/reads/fake-play-chats-show.json', 'data/protocol/fake-play.jsonl'])
    expect(filesUnder(fixtures).sort()).toEqual(['business/reads/fake-play-chats-show.json', 'data/protocol/fake-play.jsonl'])
    expect(readFileSync(join(fixtures, 'data/protocol/fake-play.jsonl'), 'utf8')).toBe(
      '{"model":"root","next_turn":1,"protocol_version":1,"chat_id":"20261119-000000-fake","type":"session"}\n' +
        '{"finish_reason":"stop","model":"root","text":"ROOT</play summarize-verify hello>","turn":1,"type":"result"}\n',
    )
    const calls = readFileSync(fake.log, 'utf8')
      .trim()
      .split('\n')
      .map((line) => JSON.parse(line) as { args: string[]; home: string; userHome: string; cwd: string })
    expect(calls.map((call) => call.args.slice(0, 2).join(' '))).toEqual([
      'config init',
      'config add-backend',
      'config add-suite',
      'config set-default',
      'execute --output-format',
      'chats show',
    ])
    expect(calls[1]?.args).toEqual(['config', 'add-backend', 'root', '--type', 'mock', '--model-path', join(calls[1]?.home ?? '', 'play-root.json')])
    expect(calls[2]?.args).toEqual(['config', 'add-suite', 'coding', '--chat', 'root', '--utility', 'root', '--orchestrate', 'on'])
    expect(calls.at(-1)?.args).toEqual(['chats', 'show', '20261119-000000-fake', '--output-format', 'json'])
    for (const call of calls) {
      expect(call.home.startsWith(temp), 'APOGEE_HOME is the throwaway home').toBe(true)
      expect(call.userHome.startsWith(temp), 'HOME is the throwaway home too').toBe(true)
    }
    const session = calls.find((call) => call.args[0] === 'execute')
    expect(session?.cwd.startsWith(realpathSync(temp)), 'the session runs in the throwaway working folder').toBe(true)
    expect(readdirSync(temp), 'the throwaway home is gone').toEqual([])
  })

  test('it refuses an output that would leave fixtures/', async () => {
    // fixtures/ sits in a scratch folder of its own, so the path it refuses lands there, and a broken guard leaks nothing.
    const base = scratch()
    const fixtures = join(base, 'fixtures')
    mkdirSync(fixtures)
    const fake = fakeBinary(scratch())
    await expect(capture({ ...recipe, output: '../escaped.jsonl' }, { binary: fake.binary, fixturesRoot: fixtures, tempRoot: scratch() })).rejects.toThrow(/outside fixtures/)
    expect(existsSync(join(base, 'escaped.jsonl'))).toBe(false)
  })

  test('a binary that exits badly is refused, its stderr said, and nothing written', async () => {
    const fixtures = scratch()
    const folder = scratch()
    const binary = join(folder, 'apogee')
    mkdirSync(folder, { recursive: true })
    writeFileSync(binary, '#!/bin/sh\necho "apogee config: refused" >&2\nexit 3\n')
    chmodSync(binary, 0o755)
    await expect(capture(recipe, { binary, fixturesRoot: fixtures, tempRoot: scratch() })).rejects.toThrow(/apogee config: refused/)
    expect(filesUnder(fixtures)).toEqual([])
  })

  test('the lines a recipe writes before its first turn go after the hello, in order (design 1v)', async () => {
    const fixtures = scratch()
    const fake = fakeBinary(scratch())
    await capture(
      { ...recipe, chatsShow: undefined, before: [{ type: 'attach', path: '.' }, { type: 'set', model: 'claude' }] },
      { binary: fake.binary, fixturesRoot: fixtures, tempRoot: scratch() },
    )
    const written = readFileSync(fake.stdin, 'utf8').trim().split('\n').map((line) => (JSON.parse(line) as { type: string }).type)
    expect(written).toEqual(['hello', 'attach', 'set', 'user'])
  })

  test('a recipe that cancels writes a cancel once the named line of its turn has come, in place of an answer, and the other turns go on as before (design 1v)', async () => {
    const fixtures = scratch()
    const fake = fakeBinary(scratch())
    await capture(
      { ...recipe, chatsShow: undefined, turns: ['ask one', 'ask two'], answers: { question: 'first' }, cancel: { at: 'question', turn: 2 } },
      { binary: fake.binary, fixturesRoot: fixtures, tempRoot: scratch() },
    )
    const written = readFileSync(fake.stdin, 'utf8').trim().split('\n').map((line) => JSON.parse(line) as { type: string; text?: string })
    expect(written.map((line) => line.type)).toEqual(['hello', 'user', 'answer', 'user', 'cancel'])
    expect(written[2]?.text).toBe('first')
    const stream = readFileSync(join(fixtures, 'data/protocol/fake-play.jsonl'), 'utf8').trim().split('\n')
    expect(JSON.parse(stream.at(-1) ?? '{}')).toMatchObject({ finish_reason: 'cancelled', turn: 2, type: 'result' })
  })

  test('each later launch resumes the captured chat in the same throwaway home, its stream written to its own output (design 1v)', async () => {
    const fixtures = scratch()
    const temp = scratch()
    const fake = fakeBinary(scratch())
    const written = await capture(
      {
        ...recipe,
        chatsShow: undefined,
        resumes: [
          { output: 'data/protocol/fake-resumed.jsonl', launch: ['chat', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools'], turns: [] },
          { output: 'data/protocol/fake-resumed-turn.jsonl', launch: ['chat', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools', '--suite', 'coding'], turns: ['again'] },
        ],
      },
      { binary: fake.binary, fixturesRoot: fixtures, tempRoot: temp },
    )
    expect(written.map((path) => relative(fixtures, path))).toEqual(['data/protocol/fake-play.jsonl', 'data/protocol/fake-resumed.jsonl', 'data/protocol/fake-resumed-turn.jsonl'])
    expect(readFileSync(join(fixtures, 'data/protocol/fake-resumed.jsonl'), 'utf8')).toBe('{"model":"root","next_turn":2,"protocol_version":1,"chat_id":"20261119-000000-fake","type":"session"}\n')
    expect(readFileSync(join(fixtures, 'data/protocol/fake-resumed-turn.jsonl'), 'utf8')).toContain('"text":"ROOT<again>","turn":2')
    const calls = readFileSync(fake.log, 'utf8')
      .trim()
      .split('\n')
      .map((line) => JSON.parse(line) as { args: string[]; home: string })
    const launches = calls.filter((call) => call.args.includes('stream-json'))
    expect(launches.map((call) => call.args.slice(5))).toEqual([['--tools', '--suite', 'coding'], ['--tools', '--resume', '20261119-000000-fake'], ['--tools', '--suite', 'coding', '--resume', '20261119-000000-fake']])
    expect(new Set(launches.map((call) => call.home)).size, 'one throwaway home for every launch').toBe(1)
    expect(readdirSync(temp), 'the throwaway home is gone').toEqual([])
  })

  test('a seeded file in a subfolder is written there, and a resume can swap a member’s actor before it launches (design 1v)', async () => {
    const fixtures = scratch()
    const fake = fakeBinary(scratch())
    await capture(
      {
        ...recipe,
        chatsShow: undefined,
        folder: { 'src/ingest/runner.py': 'print(1)\n' },
        resumes: [{ output: 'data/protocol/fake-resumed.jsonl', launch: ['chat', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools'], turns: ['again'], actors: { root: 'play-helper.json' } }],
      },
      { binary: fake.binary, fixturesRoot: fixtures, tempRoot: scratch() },
    )
    const launches = readFileSync(fake.log, 'utf8')
      .trim()
      .split('\n')
      .map((line) => JSON.parse(line) as { args: string[]; actors: Record<string, string>; seeded: string | null })
      .filter((call) => call.args.includes('stream-json'))
    expect(launches.map((call) => call.seeded)).toEqual(['print(1)\n', 'print(1)\n'])
    const actor = (name: string) => readFileSync(join(prototype, 'scripts', 'actors', name), 'utf8')
    expect(launches[0]?.actors['play-root.json']).toBe(actor('play-root.json'))
    expect(launches[1]?.actors['play-root.json'], 'the resume runs root over the swapped actor').toBe(actor('play-helper.json'))
  })

  test('a seeded PDF is a real one: each xref offset lands on its object, one page per text (design 1v)', () => {
    const text = minimalPdf(['first page', 'second (page) \\ two'])
    expect(text.startsWith('%PDF-1.4\n')).toBe(true)
    const at = text.indexOf('\nxref\n') + 1
    const xref = text.slice(at)
    const offsets = [...xref.matchAll(/^(\d{10}) 00000 n $/gm)].map((match) => Number(match[1]))
    expect(offsets.length).toBe(1 + 1 + 2 * 2 + 1)
    offsets.forEach((offset, index) => expect(text.slice(offset, offset + `${index + 1} 0 obj`.length)).toBe(`${index + 1} 0 obj`))
    expect(text).toContain('(second \\(page\\) \\\\ two) Tj')
    expect(Number(/startxref\n(\d+)\n%%EOF\n$/.exec(text)?.[1])).toBe(at)
  })

  test('a resume refuses an output outside fixtures/ before anything runs', async () => {
    const base = scratch()
    const fixtures = join(base, 'fixtures')
    mkdirSync(fixtures)
    const fake = fakeBinary(scratch())
    await expect(
      capture({ ...recipe, resumes: [{ output: '../escaped.jsonl', launch: ['chat'], turns: [] }] }, { binary: fake.binary, fixturesRoot: fixtures, tempRoot: scratch() }),
    ).rejects.toThrow(/outside fixtures/)
    expect(existsSync(fake.log)).toBe(false)
  })

  test("each recipe's actor files are in scripts/actors/, outside fixtures/, each a mock script of turns", () => {
    expect(Object.keys(recipes)).toContain('execute-play-summarize-verify')
    for (const entry of Object.values(recipes)) {
      expect(entry.output.startsWith('data/protocol/')).toBe(true)
      for (const backend of entry.backends) {
        const actor = JSON.parse(readFileSync(join(prototype, 'scripts', 'actors', backend.actor), 'utf8')) as { turns: unknown[] }
        expect(Array.isArray(actor.turns), backend.actor).toBe(true)
      }
    }
  })
})

/**
 * A fake `apogee` for a command recipe: it logs each call's arguments and
 * homes, remembers what `config set` set in its home, and prints it back on
 * `config get` -- an empty line for a key never set, as the CLI does.
 */
function fakeConfig(folder: string): { binary: string; log: string } {
  const log = join(folder, 'calls.log')
  const binary = join(folder, 'apogee')
  writeFileSync(
    binary,
    `#!/usr/bin/env node
const fs = require('node:fs')
const args = process.argv.slice(2)
fs.appendFileSync(${JSON.stringify(log)}, JSON.stringify({ args, home: process.env.APOGEE_HOME, userHome: process.env.HOME }) + '\\n')
const store = process.env.APOGEE_HOME + '/set.json'
const set = fs.existsSync(store) ? JSON.parse(fs.readFileSync(store, 'utf8')) : {}
if (args[0] === 'config' && args[1] === 'set') { set[args[2]] = args[3]; fs.writeFileSync(store, JSON.stringify(set)); process.stdout.write(args[2] + ' = ' + args[3] + '\\n') }
if (args[0] === 'config' && args[1] === 'get') process.stdout.write((set[args[2]] ?? '') + '\\n')
process.exit(0)
`,
  )
  chmodSync(binary, 0o755)
  return { binary, log }
}

const command: CommandRecipe = {
  name: 'fake-get',
  output: 'business/reads/fake-get.txt',
  setup: [['config', 'set', 'tools.search.url', 'http://searx.home.arpa:8080']],
  command: ['config', 'get', 'tools.search.url'],
}

describe('a command recipe: one plain read recorded from the branch build, in a throwaway home (design 1i)', () => {
  test('it runs config init, the setup and the command in a throwaway home, writes only its stdout under fixtures/, and leaves no temp folder', async () => {
    const fixtures = scratch()
    const temp = scratch()
    const fake = fakeConfig(scratch())
    const written = await captureCommand(command, { binary: fake.binary, fixturesRoot: fixtures, tempRoot: temp })
    expect(written.map((path) => relative(fixtures, path))).toEqual(['business/reads/fake-get.txt'])
    expect(filesUnder(fixtures)).toEqual(['business/reads/fake-get.txt'])
    expect(readFileSync(join(fixtures, 'business/reads/fake-get.txt'), 'utf8')).toBe('http://searx.home.arpa:8080\n')
    const calls = readFileSync(fake.log, 'utf8')
      .trim()
      .split('\n')
      .map((line) => JSON.parse(line) as { args: string[]; home: string; userHome: string })
    expect(calls.map((call) => call.args.join(' '))).toEqual(['config init', 'config set tools.search.url http://searx.home.arpa:8080', 'config get tools.search.url'])
    for (const call of calls) {
      expect(call.home.startsWith(temp), 'APOGEE_HOME is the throwaway home').toBe(true)
      expect(call.userHome.startsWith(temp), 'HOME is the throwaway home too').toBe(true)
    }
    expect(readdirSync(temp), 'the throwaway home is gone').toEqual([])
  })

  test('with no setup, what a fresh config answers: an unset key is an empty line', async () => {
    const fixtures = scratch()
    const fake = fakeConfig(scratch())
    await captureCommand({ ...command, setup: [] }, { binary: fake.binary, fixturesRoot: fixtures, tempRoot: scratch() })
    expect(readFileSync(join(fixtures, 'business/reads/fake-get.txt'), 'utf8')).toBe('\n')
  })

  test('it refuses a missing binary, an output outside fixtures/ and a command that exits badly, and writes nothing', async () => {
    const base = scratch()
    const fixtures = join(base, 'fixtures')
    mkdirSync(fixtures)
    await expect(captureCommand(command, { binary: join(base, 'no-such-apogee'), fixturesRoot: fixtures, tempRoot: scratch() })).rejects.toThrow(/no executable apogee/)
    const fake = fakeConfig(scratch())
    await expect(captureCommand({ ...command, output: '../escaped.txt' }, { binary: fake.binary, fixturesRoot: fixtures, tempRoot: scratch() })).rejects.toThrow(/outside fixtures/)
    const folder = scratch()
    const failing = join(folder, 'apogee')
    writeFileSync(failing, '#!/bin/sh\nif [ "$1 $2" = "config get" ]; then echo "apogee config: no such config key" >&2; exit 1; fi\nexit 0\n')
    chmodSync(failing, 0o755)
    await expect(captureCommand(command, { binary: failing, fixturesRoot: fixtures, tempRoot: scratch() })).rejects.toThrow(/no such config key/)
    expect(filesUnder(fixtures)).toEqual([])
    expect(existsSync(join(base, 'escaped.txt'))).toBe(false)
  })

  test("its recipes: the web search address set and unset, and recall's flag on a fresh config, each under fixtures/business/reads/", () => {
    expect(Object.keys(commandRecipes)).toEqual(['config-get-search-url', 'config-get-search-url-unset', 'config-get-memory-recall'])
    for (const [name, entry] of Object.entries(commandRecipes)) {
      expect(entry.name).toBe(name)
      expect(entry.output).toBe(`business/reads/${name}.txt`)
      expect(entry.command.slice(0, 2)).toEqual(['config', 'get'])
    }
    expect(commandRecipes['config-get-search-url']?.setup).toEqual([['config', 'set', 'tools.search.url', 'http://searx.home.arpa:8080']])
  })
})
