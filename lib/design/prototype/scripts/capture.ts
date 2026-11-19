/**
 * The design's capture script (design 1u): records a machine-mode stream from
 * the CLI's branch build, deliberately, for a preset nobody had recorded.
 *
 *   npm run capture -- <recipe>                      # the branch build
 *   npm run capture -- <recipe> --binary <apogee>    # another build of the CLI
 *
 * It follows lib/src/darwin/scripts/capture-fixture.sh's recipe, which stays
 * as it is: a throwaway home and working folder (made under the system's temp
 * folder and removed after), the CLI's scripted mock backend over actor files
 * (scripts/actors/, outside fixtures/, so an actor edit never stales the
 * build), the driver's answers, and the stream written under fixtures/ --
 * laid out as the Mac app's tree. No model, no network: the mock members run
 * through the real harness, so a tool reads the throwaway folder.
 *
 * It runs only the binary it is given, by default the branch build at
 * lib/src/cli/build/macos-arm64/source/apogee, with `APOGEE_HOME` and `HOME`
 * both the throwaway home; it writes nothing but the recipe's outputs under
 * fixtures/ and its own temp folder. Not part of `npm run check`.
 */
import { spawn, spawnSync } from 'node:child_process'
import { accessSync, constants, copyFileSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

/** One capture: the folder the session works in, the mock members and their actors, the launch, and what is sent. */
export interface Recipe {
  readonly name: string
  /** Where the stream goes, relative to fixtures/: `data/protocol/<name>.jsonl`. */
  readonly output: string
  /** The working folder's files, by name, and their text. */
  readonly folder: Readonly<Record<string, string>>
  /** The mock backends, each over an actor file in scripts/actors/. */
  readonly backends: ReadonlyArray<{ readonly name: string; readonly actor: string }>
  readonly suites?: ReadonlyArray<{ readonly name: string; readonly chat: string; readonly utility?: string; readonly orchestrate?: 'on' | 'off' }>
  /** `models.default`. */
  readonly defaultBackend: string
  /** The arguments after `apogee`: `execute`/`chat` with both directions in stream-json. */
  readonly launch: readonly string[]
  /** Each turn's `user` line, sent once the turn before has its result. */
  readonly turns: readonly string[]
  /** The driver's answers: to a question, and to the permission gate. */
  readonly answers?: { readonly question?: string; readonly permission?: string }
  /** Where to save `chats show <chat_id>` from the throwaway home after the run, relative to fixtures/. */
  readonly chatsShow?: string
}

/** The recipes this prototype records. */
export const recipes: Readonly<Record<string, Recipe>> = {
  /**
   * `chat:play`: one `apogee execute` session over a seeded folder (an
   * AGENTS.md of 12 words and one .py file, the two files the old notes
   * count, 95 bytes in all), playing summarize-verify on two mock members.
   */
  'execute-play-summarize-verify': {
    name: 'execute-play-summarize-verify',
    output: 'data/protocol/execute-play-summarize-verify.jsonl',
    folder: {
      'AGENTS.md': 'Notes for agents: keep the minutes short, and name every owner plainly.\n',
      'minutes.py': 'def f(t):\n    return t\n',
    },
    backends: [
      { name: 'root', actor: 'play-root.json' },
      { name: 'helper', actor: 'play-helper.json' },
    ],
    suites: [{ name: 'coding', chat: 'root', utility: 'helper', orchestrate: 'on' }],
    defaultBackend: 'root',
    launch: ['execute', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools', '--suite', 'coding'],
    turns: ['/play summarize-verify The board met on 14 October. It agreed to move the release to 3 November, and Ana Ruiz will own the checklist.'],
  },
}

/** A capture refused before or while it ran: one sentence saying why. */
export class CaptureRefusal extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'CaptureRefusal'
  }
}

/** The CLI's branch build, from the prototype's root. */
export function branchBinary(prototype: string): string {
  return resolve(prototype, '..', '..', 'src', 'cli', 'build', 'macos-arm64', 'source', 'apogee')
}

/** Where the capture runs and writes. */
export interface CaptureOptions {
  readonly binary: string
  /** The design's fixture root, lib/design/prototype/fixtures. */
  readonly fixturesRoot: string
  /** Where the throwaway home is made: the system's temp folder unless a test names another. */
  readonly tempRoot?: string
  /** The actor files' folder: scripts/actors. */
  readonly actorsRoot?: string
}

const scriptsFolder = dirname(fileURLToPath(import.meta.url))

/** A path under `root`, refused when it would leave it. */
function under(root: string, path: string): string {
  const full = resolve(root, path)
  const inside = relative(root, full)
  if (inside === '' || inside.startsWith('..') || resolve(inside) === inside) throw new CaptureRefusal(`capture: ${path} is outside fixtures/`)
  return full
}

/** Runs the recipe; returns the files it wrote, each under fixtures/. */
export async function capture(recipe: Recipe, options: CaptureOptions): Promise<string[]> {
  try {
    accessSync(options.binary, constants.X_OK)
  } catch {
    throw new CaptureRefusal(`capture: no executable apogee at ${options.binary}`)
  }
  const output = under(options.fixturesRoot, recipe.output)
  const chatsShow = recipe.chatsShow === undefined ? undefined : under(options.fixturesRoot, recipe.chatsShow)
  const actors = options.actorsRoot ?? join(scriptsFolder, 'actors')

  const sandbox = mkdtempSync(join(options.tempRoot ?? tmpdir(), 'apogee-capture-'))
  try {
    const home = join(sandbox, 'home')
    const work = join(sandbox, 'ingest')
    mkdirSync(home, { recursive: true })
    mkdirSync(work, { recursive: true })
    for (const [name, text] of Object.entries(recipe.folder)) writeFileSync(join(work, name), text)
    const env = { ...process.env, APOGEE_HOME: home, HOME: home }

    const run = (args: readonly string[]): string => {
      const result = spawnSync(options.binary, args, { cwd: work, env, encoding: 'utf8', input: '' })
      if (result.status !== 0) throw new CaptureRefusal(`capture: apogee ${args.join(' ')} exited ${result.status ?? result.signal}: ${(result.stderr ?? '').trim()}`)
      return result.stdout
    }

    run(['config', 'init'])
    for (const backend of recipe.backends) {
      const actor = join(home, backend.actor)
      copyFileSync(join(actors, backend.actor), actor)
      run(['config', 'add-backend', backend.name, '--type', 'mock', '--model-path', actor])
    }
    for (const suite of recipe.suites ?? []) {
      run([
        'config',
        'add-suite',
        suite.name,
        '--chat',
        suite.chat,
        ...(suite.utility === undefined ? [] : ['--utility', suite.utility]),
        ...(suite.orchestrate === undefined ? [] : ['--orchestrate', suite.orchestrate]),
      ])
    }
    run(['config', 'set-default', recipe.defaultBackend])

    const stream = await drive(options.binary, recipe, work, env)
    const files: string[] = []
    mkdirSync(dirname(output), { recursive: true })
    writeFileSync(output, stream)
    files.push(output)

    if (chatsShow !== undefined) {
      const id = chatIdOf(stream)
      if (id === undefined) throw new CaptureRefusal('capture: the session named no chat_id, so there is no chats show to save')
      const document = run(['chats', 'show', id, '--output-format', 'json'])
      mkdirSync(dirname(chatsShow), { recursive: true })
      writeFileSync(chatsShow, document)
      files.push(chatsShow)
    }
    return files
  } finally {
    rmSync(sandbox, { recursive: true, force: true })
  }
}

/** The chat the stream's `session` line names. */
function chatIdOf(stream: string): string | undefined {
  for (const line of stream.split('\n')) {
    if (line.trim() === '') continue
    const event = JSON.parse(line) as { type?: string; chat_id?: string }
    if (event.type === 'session') return event.chat_id
  }
  return undefined
}

/** The session driven as the app drives it: hello, then each turn once the last has its result, the driver's answers, then its input closed. */
function drive(binary: string, recipe: Recipe, work: string, env: NodeJS.ProcessEnv): Promise<string> {
  return new Promise((done, fail) => {
    const child = spawn(binary, [...recipe.launch], { cwd: work, env, stdio: ['pipe', 'pipe', 'pipe'] })
    let stdout = ''
    let stderr = ''
    let rest = ''
    let next = 0
    const send = (line: object) => child.stdin.write(`${JSON.stringify(line)}\n`)
    const sendTurn = () => {
      const text = recipe.turns[next]
      next += 1
      if (text === undefined) child.stdin.end()
      else send({ type: 'user', text })
    }
    const timer = setTimeout(() => child.kill('SIGTERM'), 60_000)
    child.stdout.setEncoding('utf8')
    child.stderr.setEncoding('utf8')
    child.stderr.on('data', (chunk: string) => (stderr += chunk))
    child.stdout.on('data', (chunk: string) => {
      stdout += chunk
      rest += chunk
      let at = rest.indexOf('\n')
      while (at >= 0) {
        const line = rest.slice(0, at)
        rest = rest.slice(at + 1)
        at = rest.indexOf('\n')
        if (line.trim() === '') continue
        const event = JSON.parse(line) as { type?: string; kind?: string }
        if (event.type === 'question') send({ type: 'answer', text: event.kind === 'permission' ? (recipe.answers?.permission ?? 'yes') : (recipe.answers?.question ?? '') })
        if (event.type === 'result' || event.type === 'error') sendTurn()
      }
    })
    child.on('error', (error) => {
      clearTimeout(timer)
      fail(new CaptureRefusal(`capture: ${error.message}`))
    })
    child.on('close', (status, signal) => {
      clearTimeout(timer)
      if (status !== 0) fail(new CaptureRefusal(`capture: apogee ${recipe.launch.join(' ')} exited ${status ?? signal}: ${stderr.trim()}`))
      else done(stdout)
    })
    send({ type: 'hello', client: { name: 'Apogee', version: 'design-capture' }, wants: ['tools'] })
    sendTurn()
  })
}

if (import.meta.main) {
  const prototype = resolve(scriptsFolder, '..')
  const args = process.argv.slice(2)
  const named = args.find((arg, index) => !arg.startsWith('--') && args[index - 1] !== '--binary')
  const binaryAt = args.indexOf('--binary')
  const binary = binaryAt >= 0 ? resolve(args[binaryAt + 1] ?? '') : branchBinary(prototype)
  const recipe = named === undefined ? undefined : recipes[named]
  if (!recipe) {
    console.error(`capture: name a recipe: ${Object.keys(recipes).join(', ')}`)
    process.exit(2)
  }
  capture(recipe, { binary, fixturesRoot: join(prototype, 'fixtures') }).then(
    (files) => {
      const home = mkdtempSync(join(tmpdir(), 'apogee-capture-'))
      const version = spawnSync(binary, ['version'], { encoding: 'utf8', input: '', env: { ...process.env, APOGEE_HOME: home, HOME: home } }).stdout.split('\n')[0]
      rmSync(home, { recursive: true, force: true })
      for (const file of files) console.log(`capture: ${relative(prototype, file)} from ${version}`)
    },
    (error: unknown) => {
      console.error(error instanceof Error ? error.message : String(error))
      process.exit(1)
    },
  )
}
