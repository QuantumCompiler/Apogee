import type { RunSettings } from '../../data/protocol/DriverLine'
import { MachineProtocol } from '../../data/protocol/MachineProtocol'
import type { ProtocolEvent } from '../../data/protocol/ProtocolEvent'
import type { AsyncStream } from '../../infrastructure/process/AsyncStream'
import type { BoundedTail } from '../../infrastructure/process/BoundedTail'
import { ChildExit } from '../../infrastructure/process/ChildExit'
import type { DiscoveryOutcome } from '../../infrastructure/process/StandInChild'
import type { ChatLaunch } from './ChatLaunch'

/** Why a session ended: the Mac app's `SessionEnd.Reason`. */
export type SessionEndReason =
  /** Its input closed, the child saved and exited cleanly. */
  | { readonly case: 'finished' }
  /** The child ended on its own, or exited badly: the stderr tail says why. */
  | { readonly case: 'failed' }
  /** It would not go when asked, and was terminated. */
  | { readonly case: 'stopped' }
  /** The binary would not start; `detail` says why. */
  | { readonly case: 'couldNotStart' }
  /** The child spoke a `protocol_version` this client does not read. */
  | { readonly case: 'unsupportedProtocol'; readonly version: number }

/** Why a session ended, with the child's stderr tail: the Mac app's `SessionEnd` (`MachineSession.swift:265-304`). */
export interface SessionEnd {
  readonly reason: SessionEndReason
  /** How the child exited; `undefined` when it never started. */
  readonly exit: ChildExit | undefined
  readonly stderrTail: BoundedTail
  /** For `couldNotStart`: the reason. */
  readonly detail: string | undefined
}

export const SessionEnd = {
  of(reason: SessionEndReason, exit: ChildExit | undefined, stderrTail: BoundedTail, detail?: string): SessionEnd {
    return { reason, exit, stderrTail, detail }
  },

  /** One line saying why, the child's own last words included where there are some. */
  message(end: SessionEnd): string {
    const said = end.stderrTail.lastLine === undefined ? '' : `: ${end.stderrTail.lastLine}`
    switch (end.reason.case) {
      case 'finished':
        return 'the session ended'
      case 'failed':
        return `apogee ${end.exit ? ChildExit.description(end.exit) : 'stopped'}${said}`
      case 'stopped':
        return `apogee did not end when asked and was stopped${said}`
      case 'couldNotStart':
        return `apogee could not be started: ${end.detail ?? 'unknown reason'}`
      case 'unsupportedProtocol':
        return `apogee speaks machine-mode protocol ${end.reason.version}; this app reads protocol ${MachineProtocol.version} -- update the app or the CLI`
    }
  },
}

/** What a session says: an event of the protocol, or its end, always the last. The Mac app's `SessionEvent`. */
export type SessionEvent = { readonly case: 'event'; readonly event: ProtocolEvent } | { readonly case: 'ended'; readonly end: SessionEnd }

/** A write to a session that has ended, or whose input is closed: the Mac app's `SessionError.notRunning`. */
export class SessionError extends Error {
  readonly case = 'notRunning'

  constructor() {
    super('the session has ended, or its input is closed')
    this.name = 'SessionError'
  }
}

/**
 * A driven chat session, as whoever renders one sees it: the Mac app's
 * `ChatSession` (`BusinessSession`, `ChatSession.swift:8-35`). `MachineSession`
 * is the prototype's, replaying a recording.
 */
export interface ChatSession {
  /** Everything the session says, in order, `.ended` last. */
  readonly events: AsyncStream<SessionEvent>
  /** Sends a turn; returns its number. */
  send(text: string): Promise<number>
  /** Answers the standing question's next item. */
  answer(text: string): Promise<void>
  /** Attaches a file, folder or glob; `graph` is `code` or `off`. */
  attach(path: string, graph?: string): Promise<void>
  /** Detaches it again (52d). */
  detach(path: string): Promise<void>
  /** Moves the run settings from the next turn (52d). */
  set(settings: RunSettings): Promise<void>
  /** Compacts the conversation now (52d). */
  compact(): Promise<void>
  /** Stops the turn in flight, the protocol's way. */
  cancel(): Promise<void>
  /** Ends the session the protocol's way: its input closes. */
  close(): Promise<void>
  /** Ends the session, escalating only as far as it must. `grace` is in milliseconds. */
  stop(grace?: number): Promise<void>
  /** Takes the stop's terminate step now. */
  terminateNow(): Promise<void>
  /** Its child's process id, when it has one. A replay has none. */
  readonly processIdentifier?: number
}

/** Where sessions come from: the binary located, a chat opened on it. The Mac app's `ChatStarting` (`ChatSession.swift:45-49`). */
export interface ChatStarting {
  locateBinary(): Promise<DiscoveryOutcome>
  /** A chat on `binary`, started as `launch` says: new unless it says to resume one. */
  openChat(binary: string, launch?: ChatLaunch): ChatSession
}
