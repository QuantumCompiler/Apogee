// The process runner: the one door every read, command and served tool call
// of the mod goes through to the apogee binary (the client invariant:
// documents in, commands out, over pipes). A run that cannot start, or is
// still running at its time, is answered as refused with why, never thrown.
import type { EngineInterface } from 'claude-code'

/** A read's time: `$.process.run`'s own default, said here once. */
export const RUN_TIMEOUT_MS = 30_000

/** The most `$.process.run` lets a run take: ten minutes. */
export const LONGEST_RUN_MS = 600_000

/** How one run is made: its time, and what is written to its stdin, then closed. */
export type RunInit = { timeoutMs?: number; stdin?: string }

/** How a run ended: what it printed and its status, or why it never ran to its end. */
export type Ran =
  | { kind: 'ran'; exitCode: number; stdout: string; stderr: string; isTruncated: boolean }
  | { kind: 'refused'; reason: string }

/** Runs `path` with `args` and waits for it to end, within its time. */
export async function runBinary($: EngineInterface, path: string, args: readonly string[], init: RunInit = {}): Promise<Ran> {
  const options = init.stdin === undefined ? { timeoutMs: init.timeoutMs ?? RUN_TIMEOUT_MS } : { timeoutMs: init.timeoutMs ?? RUN_TIMEOUT_MS, stdin: init.stdin }
  try {
    const ran = await $.process.run([path, ...args], options)

    return { kind: 'ran', exitCode: ran.exitCode, stdout: ran.stdout, stderr: ran.stderr, isTruncated: ran.isStdoutTruncated || ran.isStderrTruncated }
  } catch (error) {
    return { kind: 'refused', reason: error instanceof Error ? error.message : String(error) }
  }
}

/**
 * Runs `path` with `args` for as long as it takes, reading what it writes as
 * it comes: a supervised run, for a command that must not be cut off (a
 * promotion writes the store). The API ends it only with the loop, or when
 * the module unloads.
 */
export async function superviseBinary($: EngineInterface, path: string, args: readonly string[]): Promise<Ran> {
  let stdout = ''
  let stderr = ''
  try {
    const stream = $.process.spawn({ argv: [path, ...args] })
    for await (const chunk of stream) {
      if (chunk.stream === 'stdout') stdout += chunk.text
      else stderr += chunk.text
    }
    const ended = await stream.result

    return ended.code === null
      ? { kind: 'refused', reason: `ended by ${ended.signal ?? 'a signal'}` }
      : { kind: 'ran', exitCode: ended.code, stdout, stderr, isTruncated: false }
  } catch (error) {
    return { kind: 'refused', reason: error instanceof Error ? error.message : String(error) }
  }
}

/** The last non-empty line of `text`, if any: what a failed run said last. */
export function lastLine(text: string): string | undefined {
  return text
    .split('\n')
    .map(line => line.trim())
    .filter(line => line !== '')
    .at(-1)
}
