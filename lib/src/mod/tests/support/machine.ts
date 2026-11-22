// A scripted machine beneath the plugin: the home folder, the files that
// exist, what each binary answers to `version`, and the toasts raised. Every
// test of the mod runs over this, never a real binary (the item's Testing:
// hermetic, over scripted $.process results).
import type { On, ProcessRunInit } from 'claude-code'
import { mock } from 'claude-code/testing'

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

export type Machine = {
  /** Every command run, with what it was run with. */
  runs: { argv: string[]; init: ProcessRunInit | undefined }[]
  /** Every toast's text and how long it was asked to stay. */
  toasts: { text: string; timeoutMs: number | undefined }[]
}

/**
 * Scripts the machine. `binaries` maps a path to what it answers; a path not
 * there does not exist. `home: null` leaves HOME unset.
 */
export function machine(on: On, binaries: Readonly<Record<string, Answer>>, home: string | null = HOME): Machine {
  const made: Machine = { runs: [], toasts: [] }
  mock.env(on, home === null ? {} : { HOME: home })
  on('fs.exists', (_, e) => {
    const answer = Object.hasOwn(binaries, e.path) ? binaries[e.path] : undefined
    if (answer !== undefined && 'unreadable' in answer) {
      return { deny: answer.unreadable }
    }

    return { value: answer !== undefined }
  })
  on('process.run', (_, e) => {
    made.runs.push({ argv: [...e.argv], init: e.init })
    const answer = binaries[e.argv[0] ?? '']
    if (answer === undefined) {
      return { deny: `${e.argv[0]}: no such file` }
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
  on('ui.toast', (_, e) => {
    made.toasts.push({ text: e.text, timeoutMs: e.timeoutMs })

    return { value: undefined }
  })
  on('session.start', (_, e) => ({ cwd: e.cwd }))

  return made
}

/** The session start the engine raises: the mod finds its binary here. */
export const START = { cwd: '/', surface: 'terminal', isInteractive: true } as const
