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
import { accessSync, constants, copyFileSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
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
  /**
   * The driver's Stop (design 1v): a `cancel` written once the `nth` (first)
   * line of type `at` -- of turn `turn`, where named -- has come, in place of
   * an answer when that line asks one.
   */
  readonly cancel?: { readonly at: string; readonly turn?: number; readonly nth?: number }
  /** Where to save `chats show <chat_id>` from the throwaway home after the run, relative to fixtures/. */
  readonly chatsShow?: string
  /** Driver lines written after the hello, before the first turn: an `attach`, a `set` (design 1v). */
  readonly before?: ReadonlyArray<Readonly<Record<string, unknown>>>
  /**
   * Later launches in the same throwaway home, each resuming the chat the
   * first one wrote (`--resume <chat_id>` after its launch), its stream
   * written to its own output under fixtures/ (design 1v).
   */
  readonly resumes?: ReadonlyArray<{
    readonly output: string
    readonly launch: readonly string[]
    readonly turns: readonly string[]
    /** Members whose actor is swapped before this launch, by name: the mock reads its turns afresh each launch. */
    readonly actors?: Readonly<Record<string, string>>
  }>
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
  ...scenarioRecipes(),
}

/**
 * A text-only PDF, one page per text, each text wrapped at about 90
 * characters (design 1v): small enough to make at capture time, real enough
 * for the CLI's `pdftotext` to read page by page, so a retrieval over it is
 * the binary's own.
 */
export function minimalPdf(pages: readonly string[]): string {
  const escape = (text: string) => text.replace(/[\\()]/g, (character) => `\\${character}`)
  const wrap = (text: string) => text.match(/.{1,90}(\s|$)/g)?.map((line) => line.trim()) ?? ['']
  const objects: string[] = [
    '<< /Type /Catalog /Pages 2 0 R >>',
    `<< /Type /Pages /Count ${pages.length} /Kids [${pages.map((_, index) => `${4 + 2 * index} 0 R`).join(' ')}] >>`,
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
  ]
  pages.forEach((text, index) => {
    const lines = wrap(text).map((line) => `(${escape(line)}) Tj 0 -14 Td`)
    const stream = `BT /F1 11 Tf 72 720 Td ${lines.join(' ')} ET`
    objects.push(`<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents ${5 + 2 * index} 0 R >>`)
    objects.push(`<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`)
  })
  let text = '%PDF-1.4\n'
  const offsets = objects.map((body, index) => {
    const at = text.length
    text += `${index + 1} 0 obj\n${body}\nendobj\n`
    return at
  })
  const xref = text.length
  text += `xref\n0 ${objects.length + 1}\n0000000000 65535 f \n${offsets.map((at) => `${String(at).padStart(10, '0')} 00000 n \n`).join('')}`
  text += `trailer\n<< /Size ${objects.length + 1} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`
  return text
}

/** A seeded file's text, from scripts/actors/seeds/ (outside fixtures/, as the actors are). */
function seed(name: string): string {
  return readFileSync(fileURLToPath(new URL(`./actors/seeds/${name}`, import.meta.url)), 'utf8')
}

/**
 * The session's scenarios (design 1v), as far as the mock can play them:
 * each scenario's words through its actor files, each branch of a question
 * or a permission its own capture from the shared opening turn. What the
 * mock cannot make (the network, a failed load, a retrieval, a recall, a
 * death mid-answer) is added by hand to a copy named `*-handmade*`, and
 * MILESTONES lists the lines added.
 */
function scenarioRecipes(): Record<string, Recipe> {
  const chat = ['chat', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools']
  const ingest = { 'config.toml': seed('config.toml'), 'src/ingest/runner.py': seed('runner.py') }
  const recipes: Record<string, Recipe> = {
    /** Streaming (`chat`): the TaskGroup session's two turns over the ingest folder, attached. */
    'chat-streaming': {
      name: 'chat-streaming',
      output: 'data/protocol/chat-streaming.jsonl',
      folder: ingest,
      backends: [{ name: 'root', actor: 'streaming-root.json' }],
      defaultBackend: 'root',
      launch: chat,
      before: [{ type: 'attach', path: '.' }],
      turns: [
        'Where do fetches start in this repo, and what cancels them?',
        'So one slow host takes the whole batch down. I want the other fetches to finish and just log the timeouts. What’s the idiomatic fix on 3.12?',
      ],
    },
    /** The kv chat (`chat:restarting`, `chat:notsent`, `chat:ended`): one turn, its `chats show`, then the chat resumed as the three presets resume it. */
    'chat-kv': {
      name: 'chat-kv',
      output: 'data/protocol/chat-kv.jsonl',
      folder: {},
      backends: [{ name: 'root', actor: 'kv-root.json' }],
      suites: [{ name: 'pair', chat: 'root' }],
      defaultBackend: 'root',
      launch: chat,
      turns: ['Where does llama.cpp decide how big the KV cache is, and what does it cost for gpt-oss-20b?'],
      chatsShow: 'business/reads/chats-show-kv.json',
      resumes: [
        { output: 'data/protocol/resumed-kv.jsonl', launch: chat, turns: [] },
        { output: 'data/protocol/resumed-kv-pair.jsonl', launch: [...chat, '--suite', 'pair'], turns: [] },
        { output: 'data/protocol/resumed-kv-turn.jsonl', launch: chat, turns: ['Would a 16-bit cache fit at 128K?'], actors: { root: 'kv-root-turn.json' } },
      ],
    },
    /** Sources (`chat:wal`): moved to the billed `claude` by `set`, the 300-page storage notes (a PDF made here, read by the CLI's pdftotext) and a postgresql.conf attached, then the WAL question answered with its citations. */
    'chat-sources': {
      name: 'chat-sources',
      output: 'data/protocol/chat-sources.jsonl',
      folder: {
        'storage-engine-design.pdf': minimalPdf(seed('storage-engine-design.txt').trimEnd().split('\n')),
        'postgresql.conf': seed('postgresql.conf'),
      },
      backends: [
        { name: 'root', actor: 'sources-root.json' },
        { name: 'claude', actor: 'sources-claude.json' },
      ],
      defaultBackend: 'root',
      launch: chat,
      before: [{ type: 'set', model: 'claude' }, { type: 'attach', path: 'storage-engine-design.pdf' }, { type: 'attach', path: 'postgresql.conf' }],
      turns: ['Why does group commit make the WAL cheaper under load? Point me to the pages.'],
    },
    /** Error (`chat:crash`), its retry: `vast` answers the screenshot's question. */
    'chat-error-retry': {
      name: 'chat-error-retry',
      output: 'data/protocol/chat-error-retry.jsonl',
      folder: { 'crash-0912.png': seed('crash-0912.png') },
      backends: [
        { name: 'vast', actor: 'error-vast.json' },
        { name: 'claude', actor: 'error-claude.json' },
      ],
      defaultBackend: 'vast',
      launch: chat,
      before: [{ type: 'attach', path: 'crash-0912.png' }],
      turns: ['What’s this stack trace telling me?'],
    },
    /** Error (`chat:crash`), its switch: `set` moves it to the billed `claude`, which answers. */
    'chat-error-switch': {
      name: 'chat-error-switch',
      output: 'data/protocol/chat-error-switch.jsonl',
      folder: { 'crash-0912.png': seed('crash-0912.png') },
      backends: [
        { name: 'vast', actor: 'error-vast.json' },
        { name: 'claude', actor: 'error-claude.json' },
      ],
      defaultBackend: 'vast',
      launch: chat,
      before: [{ type: 'attach', path: 'crash-0912.png' }, { type: 'set', model: 'claude' }],
      turns: ['What’s this stack trace telling me?'],
    },
    /**
     * Streaming (`chat`), stopped: the same two turns, the follow-up's
     * reasoning streamed slowly (`delay_ms`) and a `cancel` written at its
     * 20th delta -- the binary's own ending of a turn stopped mid-reasoning.
     */
    'chat-streaming-cancel': {
      name: 'chat-streaming-cancel',
      output: 'data/protocol/chat-streaming-cancel.jsonl',
      folder: ingest,
      backends: [{ name: 'root', actor: 'streaming-root-cancel.json' }],
      defaultBackend: 'root',
      launch: chat,
      before: [{ type: 'attach', path: '.' }],
      turns: [
        'Where do fetches start in this repo, and what cancels them?',
        'So one slow host takes the whole batch down. I want the other fetches to finish and just log the timeouts. What’s the idiomatic fix on 3.12?',
      ],
      cancel: { at: 'thinking_delta', turn: 2, nth: 20 },
    },
    /** Permission (`chat:retry`), stopped while its gate waits: the gated call failed, then the turn's cancelled result. */
    'chat-permission-cancel': {
      name: 'chat-permission-cancel',
      output: 'data/protocol/chat-permission-cancel.jsonl',
      folder: ingest,
      backends: [{ name: 'root', actor: 'permission-root-allowed.json' }],
      defaultBackend: 'root',
      launch: chat,
      turns: ['Make `_fetch()` retry timeouts and connection errors, using `settings.fetch.retries` (it’s already in config.toml). Go ahead and edit the file.'],
      cancel: { at: 'question' },
    },
    /** Question (`chat:pr`), stopped while it asks: the turn's cancelled result, with the prose streamed before the question. */
    'chat-question-cancel': {
      name: 'chat-question-cancel',
      output: 'data/protocol/chat-question-cancel.jsonl',
      folder: { 'src/upload/uploader.py': seed('uploader.py') },
      backends: [{ name: 'claude', actor: 'question-claude-batch.json' }],
      defaultBackend: 'claude',
      launch: chat,
      turns: ['Review the retry-budget change in PR 482 before I merge it.'],
      cancel: { at: 'question' },
    },
  }
  /** Permission (`chat:retry`): two reads, then `write_file` behind the gate, one capture per answer. */
  for (const answer of ['yes', 'session', 'always', 'no'] as const) {
    recipes[`chat-permission-${answer}`] = {
      name: `chat-permission-${answer}`,
      output: `data/protocol/chat-permission-${answer}.jsonl`,
      folder: ingest,
      backends: [{ name: 'root', actor: answer === 'no' ? 'permission-root-denied.json' : 'permission-root-allowed.json' }],
      defaultBackend: 'root',
      launch: chat,
      turns: ['Make `_fetch()` retry timeouts and connection errors, using `settings.fetch.retries` (it’s already in config.toml). Go ahead and edit the file.'],
      answers: { permission: answer },
    }
  }
  /** Question (`chat:pr`): `ask_user` answered each way, Skip as an empty answer, one capture per answer. */
  const answers: Readonly<Record<string, string>> = { upload: 'Per upload', batch: 'Per batch', host: 'Per host', own: 'Per endpoint, shared by every upload to it', skip: '' }
  for (const [key, answer] of Object.entries(answers)) {
    recipes[`chat-question-${key}`] = {
      name: `chat-question-${key}`,
      output: `data/protocol/chat-question-${key}.jsonl`,
      folder: { 'src/upload/uploader.py': seed('uploader.py') },
      backends: [{ name: 'claude', actor: `question-claude-${key}.json` }],
      defaultBackend: 'claude',
      launch: chat,
      turns: ['Review the retry-budget change in PR 482 before I merge it.'],
      answers: { question: answer },
    }
  }
  return recipes
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
  const resumes = (recipe.resumes ?? []).map((resume) => ({ ...resume, path: under(options.fixturesRoot, resume.output) }))
  const actors = options.actorsRoot ?? join(scriptsFolder, 'actors')

  const sandbox = mkdtempSync(join(options.tempRoot ?? tmpdir(), 'apogee-capture-'))
  try {
    const home = join(sandbox, 'home')
    const work = join(sandbox, 'ingest')
    mkdirSync(home, { recursive: true })
    mkdirSync(work, { recursive: true })
    for (const [name, text] of Object.entries(recipe.folder)) {
      mkdirSync(dirname(join(work, name)), { recursive: true })
      writeFileSync(join(work, name), text)
    }
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

    const stream = await drive(options.binary, recipe.launch, recipe.turns, recipe, work, env)
    const files: string[] = []
    mkdirSync(dirname(output), { recursive: true })
    writeFileSync(output, stream)
    files.push(output)

    const id = chatIdOf(stream)
    if (chatsShow !== undefined) {
      if (id === undefined) throw new CaptureRefusal('capture: the session named no chat_id, so there is no chats show to save')
      const document = run(['chats', 'show', id, '--output-format', 'json'])
      mkdirSync(dirname(chatsShow), { recursive: true })
      writeFileSync(chatsShow, document)
      files.push(chatsShow)
    }
    for (const resume of resumes) {
      if (id === undefined) throw new CaptureRefusal('capture: the session named no chat_id, so there is no chat to resume')
      for (const [name, actor] of Object.entries(resume.actors ?? {})) {
        const backend = recipe.backends.find((entry) => entry.name === name)
        if (!backend) throw new CaptureRefusal(`capture: a resume swaps the actor of ${name}, which the recipe does not configure`)
        copyFileSync(join(actors, actor), join(home, backend.actor))
      }
      const resumed = await drive(options.binary, [...resume.launch, '--resume', id], resume.turns, { ...recipe, before: [] }, work, env)
      mkdirSync(dirname(resume.path), { recursive: true })
      writeFileSync(resume.path, resumed)
      files.push(resume.path)
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

/** The session driven as the app drives it: hello, the lines before the first turn, then each turn once the last has its result, the driver's answers (or its cancel), then its input closed. */
function drive(binary: string, launch: readonly string[], turns: readonly string[], recipe: Recipe, work: string, env: NodeJS.ProcessEnv): Promise<string> {
  return new Promise((done, fail) => {
    const child = spawn(binary, [...launch], { cwd: work, env, stdio: ['pipe', 'pipe', 'pipe'] })
    let stdout = ''
    let stderr = ''
    let rest = ''
    let next = 0
    /** The lines the recipe's cancel waits for, seen so far; once it has been written, no other. */
    let seen = 0
    let cancelled = false
    const send = (line: object) => child.stdin.write(`${JSON.stringify(line)}\n`)
    const cancelsAt = (event: { type?: string; turn?: number }): boolean => {
      const cancel = recipe.cancel
      if (!cancel || cancelled || event.type !== cancel.at || (cancel.turn !== undefined && event.turn !== cancel.turn)) return false
      seen += 1
      if (seen < (cancel.nth ?? 1)) return false
      cancelled = true
      return true
    }
    const sendTurn = () => {
      const text = turns[next]
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
        const event = JSON.parse(line) as { type?: string; kind?: string; turn?: number }
        if (cancelsAt(event)) send({ type: 'cancel' })
        else if (event.type === 'question') send({ type: 'answer', text: event.kind === 'permission' ? (recipe.answers?.permission ?? 'yes') : (recipe.answers?.question ?? '') })
        if (event.type === 'result' || event.type === 'error') sendTurn()
      }
    })
    child.on('error', (error) => {
      clearTimeout(timer)
      fail(new CaptureRefusal(`capture: ${error.message}`))
    })
    child.on('close', (status, signal) => {
      clearTimeout(timer)
      if (status !== 0) fail(new CaptureRefusal(`capture: apogee ${launch.join(' ')} exited ${status ?? signal}: ${stderr.trim()}`))
      else done(stdout)
    })
    send({ type: 'hello', client: { name: 'Apogee', version: 'design-capture' }, wants: ['tools'] })
    for (const line of recipe.before ?? []) send(line)
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
