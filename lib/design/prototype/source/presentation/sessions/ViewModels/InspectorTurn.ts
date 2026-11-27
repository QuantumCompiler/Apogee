import type { SessionEvent } from '../../../business/session/ChatSession'
import type { Question, RecalledChat, Source, ToolCall, TurnResult } from '../../../data/protocol/ProtocolEvent'

/**
 * What the open session's latest turn said, as its replayed events say it
 * (design 1g): the inspector's THIS TURN rows come from here, never from a
 * table per scenario. A pure fold of the session's events -- its tool calls
 * (which the transcript passes over), its helper lines, what it recalled and
 * retrieved, the question it asked, its error and its result -- the latest
 * turn's alone: a later turn starts it afresh, and so does a child whose
 * `session` line numbers its turns from at or below the one held (a private
 * conversation started again numbers from 1).
 */
export interface InspectorTurn {
  /** The turn's number, as the CLI numbers it. */
  readonly turn: number
  /** It thought, at least once. */
  readonly thought: boolean
  /** The lines the CLI said of its helpers and stages (`utility — rewriting …`), in order. */
  readonly helpers: readonly string[]
  /** Each call dispatched, by its id, in the order first seen, at its last state. */
  readonly calls: readonly ToolCall[]
  readonly recalled: readonly RecalledChat[]
  readonly sources: readonly Source[]
  readonly question: Question | undefined
  /** The CLI's words, when the turn failed. */
  readonly error: string | undefined
  readonly result: TurnResult | undefined
}

const fresh = (turn: number): InspectorTurn => ({ turn, thought: false, helpers: [], calls: [], recalled: [], sources: [], question: undefined, error: undefined, result: undefined })

export const InspectorTurn = {
  /** `latest` with `event` folded in; `undefined` until a turn has said anything. */
  apply(latest: InspectorTurn | undefined, event: SessionEvent): InspectorTurn | undefined {
    if (event.case !== 'event') return latest
    // A child that numbers its turns afresh (a private or unsaved conversation started again) starts THIS TURN afresh; a resumed one numbers on, and keeps the turn before.
    if (event.event.kind.case === 'session') return latest !== undefined && event.event.kind.session.nextTurn > latest.turn ? latest : undefined
    const turn = event.event.turn
    if (turn === undefined) return latest
    if (latest !== undefined && turn < latest.turn) return latest
    const state = latest === undefined || turn > latest.turn ? fresh(turn) : latest
    const kind = event.event.kind
    switch (kind.case) {
      case 'thinking':
      case 'thinkingDelta':
        return state.thought ? state : { ...state, thought: true }
      case 'toolStatus':
        // A call's own line (`[tool] read_file`) is its tool_call's; every other line is a helper's or a stage's.
        return kind.text.startsWith('[tool] ') ? state : { ...state, helpers: [...state.helpers, kind.text] }
      case 'toolCall': {
        const known = state.calls.findIndex((call) => call.id === kind.call.id)
        const calls = known < 0 ? [...state.calls, kind.call] : state.calls.map((call, index) => (index === known ? { ...kind.call, target: kind.call.target ?? call.target } : call))
        return { ...state, calls }
      }
      case 'memory':
        return { ...state, recalled: event.event.recalled }
      case 'sources':
        return { ...state, sources: kind.sources }
      case 'question':
        return { ...state, question: kind.question }
      case 'error':
        return { ...state, error: kind.message }
      case 'result':
        return { ...state, result: kind.result }
      default:
        return state
    }
  },
}
