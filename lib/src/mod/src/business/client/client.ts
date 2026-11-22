// The Apogee client: the binary discovery found, and its documents read
// through the process runner (the client invariant: documents in, tolerant
// reads, nothing parsed from prose where a document exists).
import type { EngineInterface } from 'claude-code'

import type { ApogeeBinary } from '../../../types'
import type { Decoded } from '../../data/documents/document'
import { readBinary } from '../../data/state/binary'
import { lastLine, runBinary } from '../../infrastructure/runner/runner'
import type { RunInit } from '../../infrastructure/runner/runner'

/** The binary as discovery found it. */
export type FoundBinary = Extract<ApogeeBinary, { kind: 'found' }>

/** A document read, with what it lacked; or why there is none: the run, or what it answered. */
export type Read<T> = { kind: 'read'; value: T; missing: string[] } | { kind: 'failed'; stage: 'run' | 'document'; reason: string }

/** What discovery found this session; undefined before it has looked. */
export async function sessionBinary($: EngineInterface): Promise<ApogeeBinary | undefined> {
  return readBinary($)
}

/**
 * Runs `args` on the binary and reads its stdout as one document. A run that
 * exits non-zero with a document is read all the same (`check` exits 1 on a
 * failure and still prints its report); with none, its last stderr line is why.
 */
export async function readDocument<T>(
  $: EngineInterface,
  binary: FoundBinary,
  args: readonly string[],
  decode: (text: string) => Decoded<T>,
  init: RunInit = {},
): Promise<Read<T>> {
  const ran = await runBinary($, binary.path, args, init)
  if (ran.kind === 'refused') {
    return { kind: 'failed', stage: 'run', reason: ran.reason }
  }
  const decoded = decode(ran.stdout)
  if (decoded.kind === 'read') {
    return decoded
  }
  if (ran.exitCode !== 0) {
    const said = lastLine(ran.stderr)

    return { kind: 'failed', stage: 'document', reason: `exited with status ${ran.exitCode}${said === undefined ? '' : `: ${said}`}` }
  }
  const cut = ran.isTruncated ? ' (its output passed 4 MiB and was cut)' : ''

  return { kind: 'failed', stage: 'document', reason: `${decoded.reason}${cut}` }
}
