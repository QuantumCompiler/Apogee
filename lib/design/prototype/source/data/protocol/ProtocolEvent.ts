import { JSONValue } from './JSONValue'
import type { JSONObject } from './JSONValue'

/** What a session can do, announced before its first turn. */
export interface Capabilities {
  /** Every event type the build can write. */
  readonly events: readonly string[]
  /** The line types the session reads. */
  readonly accepts: readonly string[]
  readonly tools: boolean
  readonly ask: boolean
  /** The vocabulary's version, a date. */
  readonly schema: string
}

/** The `session` event's facts. */
export interface SessionInfo {
  readonly protocolVersion: number
  readonly model: string
  /** The number the next `user` line will get. */
  readonly nextTurn: number
  /** Absent from a binary that predates them. */
  readonly capabilities: Capabilities | undefined
  /** A chat that writes nothing (`chat --private`). */
  readonly isPrivate: boolean
  /** The chat the session writes (`chat_id`, the CLI's M28); a value that is no id is none. */
  readonly chatID: string | undefined
  /** The suite it runs under (`suite`, the CLI's M29). */
  readonly suite: string | undefined
  /** An `execute` session's: whether its root model may play the symphonies (M29). */
  readonly orchestrating: boolean | undefined
}

/** One excerpt a turn's retrieval sent the model. */
export interface Source {
  readonly file: string
  readonly pages: readonly number[] | undefined
  readonly lines: readonly number[] | undefined
  readonly times: readonly number[] | undefined
  readonly score: number | undefined
  /** `lexical`, `vector` or `hybrid`. */
  readonly retriever: string | undefined
}

/** A `tool_call` event (the CLI's 56a): one dispatched call's state. */
export interface ToolCall {
  readonly id: string
  readonly name: string
  /** `pending`, `running`, `done` or `failed`, or a state a later build adds. */
  readonly state: string
  readonly target: string | undefined
}

export interface TodoItem {
  readonly text: string
  /** `pending`, `active` or `done`, or a state a later build adds. */
  readonly state: string
}

export interface Usage {
  readonly inputTokens: number | undefined
  readonly outputTokens: number | undefined
}

/** A turn's `result`. */
export interface TurnResult {
  readonly text: string
  readonly model: string | undefined
  /** `stop`, `cancelled` …, a string that may grow. */
  readonly finishReason: string | undefined
  /** Absent when the provider reported none: absent is not zero. */
  readonly usage: Usage | undefined
}

export interface QuestionOption {
  readonly label: string
  readonly description: string
}

export interface QuestionItem {
  readonly header: string
  readonly question: string
  readonly multiSelect: boolean
  /** A convenience, not a constraint: free text is always accepted. */
  readonly options: readonly QuestionOption[]
}

/** A `question` event: one or more questions, each answered by one `answer` line, in order. */
export interface Question {
  /** `permission` for the permission gate; absent for `ask_user`. */
  readonly kind: string | undefined
  readonly tool: string | undefined
  readonly target: string | undefined
  /** Whether answering yes sends data off the machine. */
  readonly outbound: boolean
  readonly detail: string | undefined
  readonly items: readonly QuestionItem[]
}

/**
 * The known events of machine-mode.md as typed cases, a task run's lifecycle
 * kept whole as `task`, everything else `unknown`: the Mac app's
 * `ProtocolEvent.Kind` (`DataProtocol`, `ProtocolEvent.swift`).
 */
export type ProtocolEventKind =
  | { readonly case: 'session'; readonly session: SessionInfo }
  | { readonly case: 'thinking'; readonly budgetReached: boolean }
  | { readonly case: 'thinkingDelta'; readonly text: string }
  | { readonly case: 'memory'; readonly chats: number; readonly decisions: number }
  | { readonly case: 'sources'; readonly sources: readonly Source[] }
  | { readonly case: 'toolStatus'; readonly text: string }
  | { readonly case: 'toolCall'; readonly call: ToolCall }
  | { readonly case: 'notice'; readonly text: string }
  | { readonly case: 'todo'; readonly items: readonly TodoItem[] }
  | { readonly case: 'answerStart' }
  | { readonly case: 'answerDelta'; readonly text: string }
  | { readonly case: 'answerEnd' }
  | { readonly case: 'result'; readonly result: TurnResult }
  | { readonly case: 'question'; readonly question: Question }
  | { readonly case: 'error'; readonly message: string }
  /** A task run's lifecycle event (`task_started`, `task_round` …), kept whole in `fields`. */
  | { readonly case: 'task'; readonly type: string }
  /** A type this client does not know, or a known type it could not read. Kept, never dropped. */
  | { readonly case: 'unknown'; readonly type: string }
  /** A line that is not one JSON object with a type: shown, not swallowed. */
  | { readonly case: 'unreadable'; readonly line: string }

/**
 * One line the `apogee` child wrote on stdout, read: its case, and every field
 * it carried, read or not. Decoding never fails (rule 1 of the protocol:
 * tolerate what is new).
 */
export class ProtocolEvent {
  constructor(
    readonly kind: ProtocolEventKind,
    readonly fields: JSONObject,
  ) {}

  /** The line's `type`. */
  get type(): string {
    return JSONValue.string(this.fields.type) ?? ''
  }

  /** The turn the event belongs to: the number of the `user` line it answers. */
  get turn(): number | undefined {
    return JSONValue.int(this.fields.turn)
  }
}
