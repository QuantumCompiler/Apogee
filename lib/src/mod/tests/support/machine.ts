// A scripted machine beneath the plugin: the home folder, the files that
// exist, what each binary answers to `version` and to the commands the mod
// runs, the toasts raised, the status line, the tools and commands
// registered, the questions asked and the clock. Every test of the mod runs
// over this, never a real binary (the item's Testing: hermetic, over scripted
// $.process results).
import type { CommandSpec, On, PaneOpenArgs, ProcessRunInit, ToolSpec, UiPane } from 'claude-code'
import { mock } from 'claude-code/testing'
import type { MockClock } from 'claude-code/testing'

export const HOME = '/Users/someone'
export const BIN = `${HOME}/.local/bin`

/**
 * `apogee version` as recorded for the macOS app's discovery tests
 * (lib/src/darwin/tests/data/discovery/BinaryDiscoveryTests.swift).
 */
export const RELEASE_VERSION = 'apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)\nchannel: release\n'

/**
 * The dev channel's report: the recorded line's shape with the channel the
 * CLI's version_report prints for `make install MODE=dev`, its root line
 * included (lib/src/cli/source/presentation/cli/version_command.cpp).
 */
export const DEV_VERSION =
  'apogee 0.2.0 (0bec6e7c, 2026-10-26, macos-arm64)\nchannel: dev\nroot: /Users/someone/.apogee-dev (the dev channel\'s own root, baked into this build)\n'

/**
 * What one binary does when run: its output and status, or a run that never
 * starts; or a path that cannot be looked at, whose `fs.exists` rejects (as
 * the API says it does for a network location, and an `fs.*` hook may).
 */
export type Answer = { exitCode?: number; stdout?: string; stderr?: string } | { refuses: string } | { unreadable: string }

/**
 * What the binary answers to a command other than `version`: by its
 * arguments (the path left off), late when the script awaits the clock;
 * undefined is a command nobody scripted, refused as one.
 */
export type Script = (args: readonly string[], init: ProcessRunInit | undefined) => Answer | undefined | Promise<Answer | undefined>

/**
 * How the person answers a question the mod asks (`$.ui.ask`): the label
 * picked, or undefined for a dialog dismissed.
 */
export type Asker = (question: string, labels: readonly string[]) => string | undefined

/** One command run: by `$.process.run` with its init, or spawned (`$.process.spawn`). */
export type Run = { argv: string[]; init: ProcessRunInit | undefined; isSpawned?: true }

export type Machine = {
  /** Every command run, with what it was run with. */
  runs: Run[]
  /** The `version` runs alone: discovery's. */
  versions: () => { argv: string[]; init: ProcessRunInit | undefined }[]
  /** Every toast's text and how long it was asked to stay. */
  toasts: { text: string; timeoutMs: number | undefined }[]
  /** Every status line set, in order; undefined for a line taken off. */
  statuses: (string | undefined)[]
  /** Every tool registered, as registered. */
  tools: ToolSpec[]
  /** Every slash command registered. */
  commands: CommandSpec[]
  /** Every question asked, with its labels. */
  asks: { question: string; labels: string[] }[]
  /** The clock beneath the plugin: it moves only when the test moves it. */
  clock: MockClock
  /** Every pane opened (`$.ui.open`), as asked. */
  opens: PaneOpenArgs[]
  /**
   * The panes the engine says are open (`$.ui.panes()`): an open adds one,
   * shown and placed unless the world says otherwise; a test changes a row
   * to hide the pane, or empties the list to close it.
   */
  panes: UiPane[]
  /** How many times the mod asked which panes are open. */
  paneLooks: () => number
}

/**
 * How the scripted world answers beyond `version`: the commands, the person,
 * the clock's start, and an engine that refuses every tool (`toolRefusal`) or
 * the tools named, each with its reason (`toolRefusals`).
 */
export type World = {
  script?: Script
  ask?: Asker
  now?: number
  toolRefusal?: string
  toolRefusals?: Readonly<Record<string, string>>
  /** Why an open waits undrawn (`isPlaced: false`), when it does; a pane is placed at once otherwise. */
  paneWaits?: string
  /** Why `$.ui.open` is refused (another plugin holds the id), when it is. */
  paneRefusal?: string
}

/**
 * Scripts the machine. `binaries` maps a path to what it answers to
 * `version`; a path not there does not exist. `home: null` leaves HOME unset.
 */
export function machine(on: On, binaries: Readonly<Record<string, Answer>>, home: string | null = HOME, world: World = {}): Machine {
  const clock = mock.clock(on, { now: world.now ?? 1_800_000_000_000 })
  const made: Machine = {
    runs: [],
    versions: () => made.runs.filter(run => run.argv[1] === 'version'),
    toasts: [],
    statuses: [],
    tools: [],
    commands: [],
    asks: [],
    clock,
    opens: [],
    panes: [],
    paneLooks: () => looks,
  }
  let looks = 0
  mock.env(on, home === null ? {} : { HOME: home })
  on('fs.exists', (_, e) => {
    const answer = Object.hasOwn(binaries, e.path) ? binaries[e.path] : undefined
    if (answer !== undefined && 'unreadable' in answer) {
      return { deny: answer.unreadable }
    }

    return { value: answer !== undefined }
  })
  on('process.run', async (_, e) => {
    made.runs.push({ argv: [...e.argv], init: e.init })
    const path = e.argv[0] ?? ''
    const args = e.argv.slice(1)
    const answer = args.length === 1 && args[0] === 'version' ? binaries[path] : Object.hasOwn(binaries, path) ? await world.script?.(args, e.init) : undefined
    if (answer === undefined) {
      return { deny: `${e.argv.join(' ')}: not scripted` }
    }
    if ('refuses' in answer) {
      return { deny: answer.refuses }
    }
    if ('unreadable' in answer) {
      return { deny: answer.unreadable }
    }

    return {
      value: {
        exitCode: answer.exitCode ?? 0,
        stdout: answer.stdout ?? '',
        stderr: answer.stderr ?? '',
        isStdoutTruncated: false,
        isStderrTruncated: false,
      },
    }
  })
  // A supervised run streams what the script answers, stdout then stderr, then how it ended.
  on('process.spawn', async function* (_, e) {
    made.runs.push({ argv: [...e.argv], init: undefined, isSpawned: true })
    const path = e.argv[0] ?? ''
    const answer = Object.hasOwn(binaries, path) ? await world.script?.(e.argv.slice(1), undefined) : undefined
    if (answer === undefined || 'refuses' in answer || 'unreadable' in answer) {
      return { deny: answer === undefined ? `${e.argv.join(' ')}: not scripted` : 'refuses' in answer ? answer.refuses : answer.unreadable }
    }
    if (answer.stdout !== undefined && answer.stdout !== '') yield { stream: 'stdout' as const, text: answer.stdout }
    if (answer.stderr !== undefined && answer.stderr !== '') yield { stream: 'stderr' as const, text: answer.stderr }

    return { value: { code: answer.exitCode ?? 0, signal: null } }
  })
  on('ui.toast', (_, e) => {
    made.toasts.push({ text: e.text, timeoutMs: e.timeoutMs })

    return { value: undefined }
  })
  on('ui.status', (_, e) => {
    made.statuses.push(e.text)

    return { value: undefined }
  })
  on('tool.register', (_, e) => {
    if (world.toolRefusal !== undefined) {
      return { deny: world.toolRefusal }
    }
    const refusal = world.toolRefusals !== undefined && Object.hasOwn(world.toolRefusals, e.name) ? world.toolRefusals[e.name] : undefined
    if (refusal !== undefined) {
      return { deny: refusal }
    }
    made.tools = [...made.tools.filter(one => one.name !== e.name), { ...e }]

    return { value: { tool: `mcp__apogee__${e.name}` } }
  })
  on('command.register', (_, e) => {
    made.commands.push({ ...e })

    return { value: { command: e.name } }
  })
  // `$.ui.ask` is a call of the AskUserQuestion tool beneath the plugin.
  on('tool.call', { tool: 'AskUserQuestion' }, (_, e) => {
    const asked = e.questions[0]
    const question = asked?.question ?? ''
    const labels = (asked?.options ?? []).map(option => option.label)
    made.asks.push({ question, labels })
    const answer = world.ask?.(question, labels)

    return { result: { questions: e.questions, answers: answer === undefined ? {} : { [question]: answer } } }
  })
  on('ui.open', (_, e) => {
    made.opens.push({ ...e })
    if (world.paneRefusal !== undefined) {
      return { deny: world.paneRefusal }
    }
    const isPlaced = world.paneWaits === undefined
    made.panes = [...made.panes.filter(pane => pane.id !== e.id), { id: e.id, title: e.title ?? e.id, isShown: isPlaced, isFocused: false, isPlaced }]

    return { value: world.paneWaits === undefined ? { isPlaced: true } : { isPlaced: false, reason: world.paneWaits } }
  })
  on('ui.panes', () => {
    looks += 1

    return { value: made.panes.map(pane => ({ ...pane })) }
  })
  on('session.start', (_, e) => ({ cwd: e.cwd }))

  return made
}

/** The session start the engine raises: the mod finds its binary here. */
export const START = { cwd: '/', surface: 'terminal', isInteractive: true } as const
