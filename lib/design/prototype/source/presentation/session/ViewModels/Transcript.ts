import { SessionEnd } from '../../../business/session/ChatSession'
import type { SessionEndReason, SessionEvent } from '../../../business/session/ChatSession'
import { MachineProtocol } from '../../../data/protocol/MachineProtocol'
import type { ProtocolEvent, Usage } from '../../../data/protocol/ProtocolEvent'
import { PlayNarration } from './Play'

/**
 * Where the turn stands: the Mac app's `Transcript.Status`, the cases this
 * part draws. Restarting and paused are 1v's and 1g's (a child stopped for
 * another, or freed while out of view).
 */
export type TranscriptStatus =
  /** Spawned; the session has not spoken. */
  | { readonly case: 'connecting' }
  /** No turn open: the input is the person's. */
  | { readonly case: 'ready' }
  /**
   * A turn is open: the model reasoning, at work (a tool, another model, a
   * pause between parts), answering, waiting on a question, or stopping
   * after a `cancel` until its result arrives.
   */
  | { readonly case: 'thinking' | 'working' | 'answering' | 'waiting' | 'stopping'; readonly turn: number }
  /** The session is over. */
  | { readonly case: 'ended' }

/** A turn's thinking section: its reasoning and the lines of what it ran. */
export interface Activity {
  /** The reasoning, as streamed: its deltas joined. */
  readonly reasoning: string
  /** Tool and helper lines, each as the CLI wrote it. */
  readonly lines: readonly string[]
  /** Open while the turn works; closed when the answer starts. */
  readonly live: boolean
  /** The reasoning was ended at its budget. */
  readonly budgetReached: boolean
  /** What the turn was sent as -- a `/play` names its symphony there. */
  readonly typed: string | undefined
}

export interface Answer {
  /** Exactly the deltas, joined. */
  readonly text: string
  readonly finished: boolean
  /** The turn's tokens, when the provider reported them: absent is not zero. */
  readonly usage: Usage | undefined
  /** The model its result says answered (`result.model`), for its footer; `undefined` until the result, or where it names none. */
  readonly model: string | undefined
}

/** The session is over, and why: the Mac app's `Ending`. */
export interface Ending {
  readonly reason: SessionEndReason
  /** One line, the child's own last words included -- for a launch it refused, its words alone. */
  readonly message: string
  /** The child's stderr tail. */
  readonly details: string
  /** The child refused the launch: it failed before its `session` event, saying why on stderr. */
  readonly atLaunch: boolean
}

/** What a session's `session` event says of its suite (the CLI's M29). */
export type SessionSuite = { readonly case: 'named'; readonly name: string } | { readonly case: 'noSuite' }

/**
 * One row's content. The cases this part draws: what the person sent, the
 * turn's activity, its answer, a notice, a stop, the session's end, and the
 * quiet line for what this app does not draw yet. The events 1v's scenarios
 * give rows of their own (a question, a permission, sources, a recall, a
 * plan, a turn's error) are that quiet line until 1v draws them; none of
 * this part's streams carries one.
 */
export type TranscriptContent =
  | { readonly case: 'user'; readonly text: string }
  | { readonly case: 'activity'; readonly activity: Activity }
  | { readonly case: 'answer'; readonly answer: Answer }
  /** A line the session said for the person, quiet, kept. */
  | { readonly case: 'notice'; readonly text: string }
  /** The turn was cancelled. */
  | { readonly case: 'stopped' }
  /** An event this client does not draw: shown, quietly, never dropped. */
  | { readonly case: 'unknown'; readonly type: string }
  | { readonly case: 'ended'; readonly ending: Ending }

/** One row of the conversation: the Mac app's `TranscriptItem`. */
export interface TranscriptItem {
  readonly id: number
  /** The turn it belongs to; `undefined` for what is said between turns. */
  readonly turn: number | undefined
  readonly content: TranscriptContent
}

/**
 * The conversation as the window shows it, folded from the session's events
 * and nothing else: the Mac app's `Transcript` (`Transcript.swift`), the
 * cases this part draws.
 *
 * A pure reducer: `apply` takes each event as it arrives, and the person's
 * own acts (`userSent`, `cancelling`) are applied the moment they are made,
 * so the window's whole behavior is a function of what was said and done --
 * testable with no window and no binary. The answer shown is exactly its
 * deltas joined; states come from events, never timers; an event this
 * client does not draw is a quiet row, never dropped. No row's text is ever
 * made here: each is an event's, or what the person sent.
 */
export class Transcript {
  items: readonly TranscriptItem[] = []
  status: TranscriptStatus = { case: 'connecting' }
  /** The model, as the CLI says it: the one the last answer said answered, else the one the `session` event named. */
  model: string | undefined = undefined
  /** The line types the session reads, as its `session` event announced them. */
  accepts: readonly string[] = []
  /** The session said it writes nothing (`"private": true`). */
  isPrivate = false
  /** The suite it runs under, as its `session` event said it (M29); `undefined` where nothing was said. */
  suite: SessionSuite | undefined = undefined
  /** An `execute` session's: whether it orchestrates, as its `session` event said (M29). */
  orchestrating: boolean | undefined = undefined
  /** The turns answered, each `result` that did not stop. */
  answeredTurns = 0
  /** Moves with every change: what a view watches. */
  revision = 0

  private nextID = 0
  private nextTurn = 1
  /** Whether the child has spoken at all. */
  private spoke = false
  /** Turns written before the child spoke, drawn where it reads them: after its launch lines. */
  private ahead: string[] = []

  /** Whether a turn may be sent now. */
  get acceptsInput(): boolean {
    return this.status.case === 'ready'
  }

  /** Whether there is a turn in flight that `cancel` would stop. */
  get turnInFlight(): boolean {
    switch (this.status.case) {
      case 'thinking':
      case 'working':
      case 'answering':
      case 'waiting':
        return true
      default:
        return false
    }
  }

  /** The turn in flight's number, or the one stopping. */
  get turn(): number | undefined {
    return 'turn' in this.status ? this.status.turn : undefined
  }

  /** The live child's refusal of its launch, when that is how the session ended. */
  get launchRefusal(): Ending | undefined {
    const last = this.items.at(-1)?.content
    return this.status.case === 'ended' && last?.case === 'ended' && last.ending.atLaunch ? last.ending : undefined
  }

  /** The play an activity's lines narrate, when they narrate one. */
  static play(activity: Activity): PlayNarration | undefined {
    return PlayNarration.of(activity.lines, activity.typed)
  }

  // MARK: - The person's acts

  /** A turn was sent; returns its number. */
  userSent(text: string): number {
    const turn = this.nextTurn
    this.nextTurn += 1
    this.append({ case: 'user', text }, turn)
    this.status = { case: 'working', turn }
    return turn
  }

  /**
   * A turn written before the child spoke, as a still preset writes its sends
   * (DEVELOPER.md → the CLI stand-in): the child reads it once it has said
   * its launch lines, so it is drawn there -- before the first event of its
   * turn -- and never, if the child ends unread.
   */
  userWroteAhead(text: string): void {
    this.ahead.push(text)
  }

  /** A `cancel` was sent for the turn in flight. */
  cancelling(): void {
    if (!this.turnInFlight || !('turn' in this.status)) return
    this.status = { case: 'stopping', turn: this.status.turn }
    this.revision += 1
  }

  // MARK: - The session's events

  apply(event: SessionEvent): void {
    if (event.case === 'event') {
      this.applyEvent(event.event)
      return
    }
    // The child is gone, mid-answer or not: what it streamed is all there is.
    this.ahead = []
    this.closeActivity()
    this.finishOpenAnswers()
    this.append({ case: 'ended', ending: Transcript.ending(event.end, this.spoke) }, undefined)
    this.status = { case: 'ended' }
  }

  /** How `end` is drawn: a child that failed before its `session` event refused the launch, in its own words. */
  static ending(end: SessionEnd, spoke: boolean): Ending {
    const said = end.stderrTail.lastLine
    if (!spoke && end.reason.case === 'failed' && said !== undefined) {
      return { reason: end.reason, message: said, details: end.stderrTail.text, atLaunch: true }
    }
    return { reason: end.reason, message: SessionEnd.message(end), details: end.stderrTail.text, atLaunch: false }
  }

  private applyEvent(event: ProtocolEvent): void {
    const turn = event.turn
    // A turn written ahead is drawn as its own turn begins: each before the first event of its number.
    while (turn !== undefined && this.ahead.length > 0 && this.nextTurn <= turn) this.userSent(this.ahead.shift() ?? '')
    const kind = event.kind
    switch (kind.case) {
      case 'session': {
        const info = kind.session
        this.model = info.model
        this.nextTurn = info.nextTurn
        this.accepts = info.capabilities?.accepts ?? []
        this.isPrivate = info.isPrivate
        // The CLI's word on its suite (M29): a name, or -- from a binary that names its suite -- none.
        const namesSuite = (info.capabilities?.schema ?? '') >= MachineProtocol.namesSuite
        if (info.suite !== undefined) this.suite = { case: 'named', name: info.suite }
        else if (namesSuite) this.suite = { case: 'noSuite' }
        if (info.orchestrating !== undefined || namesSuite) this.orchestrating = info.orchestrating
        this.spoke = true
        if (this.status.case === 'connecting') this.status = { case: 'ready' }
        this.revision += 1
        return
      }
      case 'thinking':
        this.updateActivity(turn, (activity) => ({ ...activity, live: true, budgetReached: activity.budgetReached || kind.budgetReached }))
        this.setStatus({ case: 'thinking', turn: turn ?? 0 })
        return
      case 'thinkingDelta':
        this.updateActivity(turn, (activity) => ({ ...activity, reasoning: activity.reasoning + kind.text }))
        return
      case 'toolStatus':
        this.updateActivity(turn, (activity) => ({ ...activity, lines: [...activity.lines, kind.text] }))
        this.setStatus({ case: 'working', turn: turn ?? 0 })
        return
      case 'toolCall':
        // A call's states (the CLI's 56a) are for a driver that draws a card
        // per call; here the call is its `tool_status` line in the activity
        // row, so its states add no row -- as the Mac app skips it.
        return
      case 'notice':
        this.append({ case: 'notice', text: kind.text }, turn)
        return
      case 'answerStart':
        this.closeActivity()
        this.append({ case: 'answer', answer: { text: '', finished: false, usage: undefined, model: undefined } }, turn)
        this.setStatus({ case: 'answering', turn: turn ?? 0 })
        return
      case 'answerDelta': {
        const index = this.openAnswer(turn)
        if (index === undefined) {
          this.closeActivity()
          this.append({ case: 'answer', answer: { text: kind.text, finished: false, usage: undefined, model: undefined } }, turn)
        } else {
          this.replace(index, (content) => (content.case === 'answer' ? { case: 'answer', answer: { ...content.answer, text: content.answer.text + kind.text } } : content))
        }
        this.setStatus({ case: 'answering', turn: turn ?? 0 })
        return
      }
      case 'answerEnd':
        this.finishAnswers(turn, undefined, undefined)
        this.setStatus({ case: 'working', turn: turn ?? 0 })
        return
      case 'result': {
        const result = kind.result
        this.closeActivity()
        const answered = this.items.some((item) => item.content.case === 'answer' && item.turn === turn)
        // A driver that dropped every delta still has the answer.
        if (!answered && result.text !== '') this.append({ case: 'answer', answer: { text: result.text, finished: true, usage: undefined, model: undefined } }, turn)
        this.finishAnswers(turn, result.usage, result.model === '' ? undefined : result.model)
        // The CLI's word for the model that answered: the label follows it.
        if (result.model !== undefined && result.model !== '') this.model = result.model
        if (result.finishReason === 'cancelled') this.append({ case: 'stopped' }, turn)
        else this.answeredTurns += 1
        this.status = { case: 'ready' }
        this.revision += 1
        return
      }
      case 'question':
        // The question's card is 1v's: the quiet line until then. The turn waits on the person all the same.
        this.closeActivity()
        this.append({ case: 'unknown', type: event.type }, turn)
        if (this.status.case !== 'stopping' && this.status.case !== 'ended') this.status = { case: 'waiting', turn: turn ?? 0 }
        return
      case 'error':
        // The turn failed: its card is 1v's (the Error scenario); the turn has ended all the same.
        this.closeActivity()
        this.finishAnswers(turn, undefined, undefined)
        this.append({ case: 'unknown', type: event.type }, turn)
        this.status = { case: 'ready' }
        this.revision += 1
        return
      case 'memory':
      case 'sources':
      case 'todo':
      case 'task':
      case 'unknown':
        this.append({ case: 'unknown', type: event.type }, turn)
        return
      case 'unreadable':
        this.append({ case: 'unknown', type: 'a line that is not JSON' }, turn)
        return
    }
  }

  // MARK: - Folding

  private append(content: TranscriptContent, turn: number | undefined): void {
    this.items = [...this.items, { id: this.nextID, turn, content }]
    this.nextID += 1
    this.revision += 1
  }

  private replace(index: number, change: (content: TranscriptContent) => TranscriptContent): void {
    const item = this.items[index]
    if (!item) return
    const content = change(item.content)
    if (content === item.content) return
    const items = [...this.items]
    items[index] = { ...item, content }
    this.items = items
    this.revision += 1
  }

  /** The status, unless a `cancel` is on its way: then the turn is stopping until its result says it stopped. */
  private setStatus(next: TranscriptStatus): void {
    if (this.status.case === 'stopping' || this.status.case === 'ended') return
    this.status = next
  }

  /** The turn's activity: the last row, when it is this turn's; a new one otherwise, so the rows stay in the order things happened. */
  private updateActivity(turn: number | undefined, change: (activity: Activity) => Activity): void {
    const last = this.items.length - 1
    const item = this.items[last]
    if (item && item.content.case === 'activity' && item.turn === turn) {
      this.replace(last, (content) => (content.case === 'activity' ? { case: 'activity', activity: change(content.activity) } : content))
      return
    }
    const fresh: Activity = { reasoning: '', lines: [], live: true, budgetReached: false, typed: this.typed(turn) }
    this.append({ case: 'activity', activity: change(fresh) }, turn)
  }

  /** What the person sent as `turn`. */
  private typed(turn: number | undefined): string | undefined {
    if (turn === undefined) return undefined
    for (const item of [...this.items].reverse()) {
      if (item.turn === turn && item.content.case === 'user') return item.content.text
    }
    return undefined
  }

  /** The live section closes: the answer started, a question stood, the turn ended. */
  private closeActivity(): void {
    this.items.forEach((item, index) => {
      if (item.content.case === 'activity' && item.content.activity.live) {
        this.replace(index, (content) => (content.case === 'activity' ? { case: 'activity', activity: { ...content.activity, live: false } } : content))
      }
    })
  }

  private openAnswer(turn: number | undefined): number | undefined {
    for (let index = this.items.length - 1; index >= 0; index -= 1) {
      const item = this.items[index]
      if (item && item.turn === turn && item.content.case === 'answer' && !item.content.answer.finished) return index
    }
    return undefined
  }

  /** Every answer still streaming finished: its child is gone, so nothing more will come of it. */
  private finishOpenAnswers(): void {
    this.items.forEach((item, index) => {
      if (item.content.case === 'answer' && !item.content.answer.finished) {
        this.replace(index, (content) => (content.case === 'answer' ? { case: 'answer', answer: { ...content.answer, finished: true } } : content))
      }
    })
  }

  private finishAnswers(turn: number | undefined, usage: Usage | undefined, model: string | undefined): void {
    this.items.forEach((item, index) => {
      if (item.turn !== turn || item.content.case !== 'answer') return
      this.replace(index, (content) =>
        content.case === 'answer'
          ? { case: 'answer', answer: { ...content.answer, finished: true, usage: usage ?? content.answer.usage, model: model ?? content.answer.model } }
          : content,
      )
    })
  }
}
