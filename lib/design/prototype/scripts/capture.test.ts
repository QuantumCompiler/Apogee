import { chmodSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, realpathSync, rmSync, statSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterEach, describe, expect, test } from 'vitest'
import { branchBinary, capture, CaptureRefusal, recipes } from './capture.ts'
import type { Recipe } from './capture.ts'

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
function fakeBinary(folder: string): { binary: string; log: string } {
  const log = join(folder, 'calls.log')
  const binary = join(folder, 'apogee')
  writeFileSync(
    binary,
    `#!/usr/bin/env node
const fs = require('node:fs')
fs.appendFileSync(${JSON.stringify(log)}, JSON.stringify({ args: process.argv.slice(2), home: process.env.APOGEE_HOME, userHome: process.env.HOME, cwd: process.cwd() }) + '\\n')
const args = process.argv.slice(2)
if (args[0] === 'chats') { process.stdout.write('{"object":"chat","id":"20261119-000000-fake","turns":[]}\\n'); process.exit(0) }
if (args.includes('stream-json')) {
  process.stdout.write('{"model":"root","next_turn":1,"protocol_version":1,"chat_id":"20261119-000000-fake","type":"session"}\\n')
  let turn = 0
  let rest = ''
  process.stdin.on('data', (chunk) => {
    rest += chunk
    let at
    while ((at = rest.indexOf('\\n')) >= 0) {
      const line = JSON.parse(rest.slice(0, at)); rest = rest.slice(at + 1)
      if (line.type === 'user') { turn += 1; process.stdout.write(JSON.stringify({ finish_reason: 'stop', model: 'root', text: 'ROOT<' + line.text + '>', turn, type: 'result' }) + '\\n') }
    }
  })
  process.stdin.on('end', () => process.exit(0))
} else process.exit(0)
`,
  )
  chmodSync(binary, 0o755)
  return { binary, log }
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
