import type { SavedChat, SavedAttachment, SavedToolCall } from '../../../business/reads/SavedChat'
import { SessionEnd } from '../../../business/session/ChatSession'
import type { SessionEndReason, SessionEvent } from '../../../business/session/ChatSession'
import { MachineProtocol } from '../../../data/protocol/MachineProtocol'
import type { ProtocolEvent, Question, RecalledChat, Source, TodoItem, Usage } from '../../../data/protocol/ProtocolEvent'
import { PlayNarration } from './Play'

/**
 * Where the turn stands: the Mac app's `Transcript.Status`. Restarting is
 * 1v's (a child stopped for another); paused, a child freed while out of
 * view, is 1g's.
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
  /**
   * Its child is being stopped so another can take it on: nothing reaches
   * the child now, so a turn waits and a line is held for the next one
   * (GUI 6a, design 1v).
   */
  | { readonly case: 'restarting' }
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

/** What a turn recalled: the chats by name, and how many decisions. The Mac app's `Recall`. */
export interface Recall {
  readonly chats: readonly RecalledChat[]
  readonly decisions: number
}

export const Recall = {
  /** `recalled 1 earlier chat`, `… and 2 decisions`. */
  summary(recall: Recall): string {
    const count = recall.chats.length
    let text = `recalled ${count} earlier ${count === 1 ? 'chat' : 'chats'}`
    if (recall.decisions > 0) text += ` and ${recall.decisions} ${recall.decisions === 1 ? 'decision' : 'decisions'}`
    return text
  },
}

/** The model asks (`ask_user`): its questions, and once answered, one answer per item. The Mac app's `QuestionCard`. */
export interface QuestionCard {
  readonly question: Question
  /** One per item, once answered; `undefined` while it stands. */
  readonly answers: readonly string[] | undefined
  /** Its turn ended, or its child did, before it was answered: it takes no answer now (the CLI drops one sent after). */
  readonly closed: boolean
}

/** The permission gate's question. The Mac app's `PermissionCard`. */
export interface PermissionCard {
  readonly question: Question
  /** `yes`, `session`, `always` or `no`, once answered. */
  readonly answer: string | undefined
  readonly closed: boolean
}

/**
 * One row's content: the Mac app's `TranscriptItem.Content`. What the person
 * sent, the turn's activity, its answer, a notice, a warning the read back
 * gave, a recall, the sources, the question and permission cards, a turn's
 * error, a turn not sent, a stop, a turn read back unanswered, the divider
 * where the live session takes over, the plan, the quiet line for what this
 * app does not know, and the session's end.
 */
export type TranscriptContent =
  | { readonly case: 'user'; readonly text: string }
  | { readonly case: 'activity'; readonly activity: Activity }
  | { readonly case: 'answer'; readonly answer: Answer }
  /** A line the session said for the person, quiet, kept. */
  | { readonly case: 'notice'; readonly text: string }
  /** What reading the saved conversation could not carry over, in the CLI's words. */
  | { readonly case: 'warning'; readonly text: string }
  /** The past chats a turn recalled, named (52a), each one openable. */
  | { readonly case: 'recalled'; readonly recall: Recall }
  /** The excerpts a turn's retrieval sent, numbered for citing (52a). */
  | { readonly case: 'sources'; readonly sources: readonly Source[] }
  | { readonly case: 'question'; readonly card: QuestionCard }
  | { readonly case: 'permission'; readonly card: PermissionCard }
  /** The turn failed, in the CLI's own words; `model`, the one the session ran on when it came (what failed to load, whatever is asked after). */
  | { readonly case: 'error'; readonly message: string; readonly model: string | undefined }
  /** The turn drawn above never reached the child: its input had closed. Its text, to be sent again (GUI 6a). */
  | { readonly case: 'notSent'; readonly text: string }
  /** The turn was cancelled. */
  | { readonly case: 'stopped' }
  /** A turn read back with no answer kept: cancelled or failed. */
  | { readonly case: 'unanswered' }
  /** Where the live session takes over from the turns read back. */
  | { readonly case: 'divider'; readonly text: string }
  /** The session's plan, drawn in place. */
  | { readonly case: 'todo'; readonly items: readonly TodoItem[] }
  /** An event this client does not draw: shown, quietly, never dropped. */
  | { readonly case: 'unknown'; readonly type: string }
  | { readonly case: 'ended'; readonly ending: Ending }

/** One row of the conversation: the Mac app's `TranscriptItem`. */
export interface TranscriptItem {
  readonly id: number
  /** The turn it belongs to; `undefined` for what is said between turns. */
  readonly turn: number | undefined
  /**
   * The stretch of the conversation it belongs to: `Transcript.earlierRange`
   * for the turns read back, else the child that said it, numbered from 1.
   * With `turn`, what a live event is matched by: a resumed child's numbers
   * can repeat ones already drawn.
   */
  readonly range: number
  readonly content: TranscriptContent
}

/**
 * The conversation as the window shows it, folded from the session's events
 * and nothing else -- and, for a saved conversation opened again, from its
 * earlier turns as `chats show` read them back: the Mac app's `Transcript`
 * (`Transcript.swift`).
 *
 * A pure reducer: `apply` takes each event as it arrives, and the person's
 * own acts (`userSent`, `answered`, `cancelling`) are applied the moment they
 * are made, so the window's whole behavior is a function of what was said
 * and done -- testable with no window and no binary. The answer shown is
 * exactly its deltas joined; states come from events, never timers; an event
 * this client does not draw is a quiet row, never dropped. No row's text is
 * ever made here: each is an event's, a read back's, or what the person sent.
 */
export class Transcript {
  /** The range of the turns read back from the saved conversation. */
  static readonly earlierRange = 0
  /** The divider's words, where the live session takes over from the turns read back. */
  static readonly earlierDivider = 'earlier in this conversation'

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
  /** The chat the session writes, as its `session` event names it (M28). */
  chatID: string | undefined = undefined
  /** The vocabulary's date its `session` event announced: what says whether the CLI names its suite itself. */
  schema: string | undefined = undefined
  /** The turns answered, each `result` that did not stop. */
  answeredTurns = 0
  /** Moves with every change: what a view watches. */
  revision = 0

  private nextID = 0
  private nextTurn = 1
  /** The range the live child's events fold into: 1 for the first child, one more for each that carries the conversation on. */
  private range = 1
  /** Whether the live child has spoken at all. */
  private spoke = false
  /** The live child has said its `session` event and has read nothing since: a notice now is one it says at launch. */
  private atLaunch = false
  /** What the live child has said at launch so far, and what the child before it said at its own: a relaunch saying the same line again draws it once. */
  private launchNotices: string[] = []
  private earlierLaunch: string[] = []
  /** Turns written before the child spoke, drawn where it reads them: after its launch lines. */
  private ahead: string[] = []
  /** Turns written before the child spoke that its closed input refused: drawn after its launch lines, once it is going or gone. */
  private refusedAhead: string[] = []

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

  /** The range the live child's events fold into. */
  get liveRange(): number {
    return this.range
  }

  /** Whether a turn written ahead has not been read yet: the child has it, so it is in flight. */
  get writtenAhead(): boolean {
    return this.ahead.length > 0
  }

  /** The question or permission card that waits on the person now, by its id; `undefined` when none does. */
  get standingCard(): number | undefined {
    for (const item of [...this.items].reverse()) {
      const content = item.content
      if ((content.case === 'question' && content.card.answers === undefined && !content.card.closed) || (content.case === 'permission' && content.card.answer === undefined && !content.card.closed)) return item.id
    }
    return undefined
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
    this.atLaunch = false
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

  /**
   * The question or permission card `id` was answered; whether it took the
   * answer. One answered already, or closed -- its turn ended first -- takes
   * none, so no answer line is sent for it.
   */
  answered(id: number, answers: readonly string[]): boolean {
    const index = this.items.findIndex((item) => item.id === id)
    const content = this.items[index]?.content
    if (content?.case === 'question' && content.card.answers === undefined && !content.card.closed) {
      this.replace(index, () => ({ case: 'question', card: { ...content.card, answers } }))
    } else if (content?.case === 'permission' && content.card.answer === undefined && !content.card.closed) {
      this.replace(index, () => ({ case: 'permission', card: { ...content.card, answer: answers[0] } }))
    } else {
      return false
    }
    if (this.status.case === 'waiting') this.status = { case: 'working', turn: this.status.turn }
    this.revision += 1
    return true
  }

  /** A `cancel` was sent for the turn in flight. */
  cancelling(): void {
    if (!this.turnInFlight || !('turn' in this.status)) return
    this.status = { case: 'stopping', turn: this.status.turn }
    this.revision += 1
  }

  /** Its child is being stopped for another to take the conversation on: until that one speaks, nothing is sent and the input says why (GUI 6a). */
  restarting(): void {
    if (this.status.case === 'ended') return
    this.status = { case: 'restarting' }
    this.revision += 1
  }

  /** A new child resumes the conversation: it waits for its `session` event, and its events fold into a range of their own. */
  reopening(): void {
    this.flushRefused()
    this.closeStandingCards()
    if (this.spoke) this.earlierLaunch = this.launchNotices
    this.launchNotices = []
    this.atLaunch = false
    this.spoke = false
    this.range += 1
    this.status = { case: 'connecting' }
    this.revision += 1
  }

  /** The person moved the session to another suite: what its last child said of its suite holds no longer, until the next child says. */
  forgetSuite(): void {
    this.suite = undefined
    this.orchestrating = undefined
    this.revision += 1
  }

  /** The app wrote the live child a line -- an attach, a setting: what it says next answers it, and is no launch notice. */
  lineSent(): void {
    this.atLaunch = false
  }

  /** A line the app says of what it did -- a move to a suite the CLI says nothing of -- quiet, kept, as the session's own notices are (the Mac app's `note`). */
  notice(text: string): void {
    this.atLaunch = false
    this.append({ case: 'notice', text }, undefined)
  }

  /** The turn `text` was drawn as sent, but its line never reached the child -- its input had closed: marked so beneath it, to be sent again (GUI 6a). */
  notSent(text: string): void {
    this.append({ case: 'notSent', text }, undefined)
  }

  /**
   * A turn written ahead that the child's closed input refused: drawn as
   * sent, and marked not sent beneath it -- where the child would have read
   * it, after its launch lines, so once it has spoken; nothing else moves
   * (GUI 6a).
   */
  refused(text: string): void {
    const at = this.ahead.indexOf(text)
    if (at >= 0) this.ahead.splice(at, 1)
    if (this.spoke) this.drawRefused(text)
    else this.refusedAhead.push(text)
  }

  private drawRefused(text: string): void {
    this.atLaunch = false
    this.append({ case: 'user', text }, undefined)
    this.notSent(text)
  }

  /** The refused turns still waiting for the child's launch lines, drawn now: it is going, or gone. */
  private flushRefused(): void {
    for (const text of this.refusedAhead.splice(0)) this.drawRefused(text)
  }

  /**
   * A saved conversation's earlier turns, as `chats show` read them back (the
   * CLI's M27), folded in as rows drawn as live turns are -- headed by what
   * the read could not carry over (55b) -- then the divider where the live
   * session takes over. Only into a transcript with no rows yet: the window
   * hands them over before the session's first event. They are a range of
   * their own: no live event is ever matched to them.
   */
  earlier(chat: SavedChat): void {
    const warnings = (chat.warnings ?? []).map((warning) => warning.message)
    if (this.items.length > 0 || (chat.turns.length === 0 && warnings.length === 0)) return
    const range = Transcript.earlierRange
    for (const warning of warnings) this.append({ case: 'warning', text: warning }, undefined, range)
    if (chat.attachments.length > 0) this.append({ case: 'notice', text: `attached to the chat: ${Transcript.named(chat.attachments)}` }, undefined, range)
    for (const entry of chat.turns) {
      if (entry.compaction !== undefined) {
        this.append({ case: 'notice', text: `history compacted; what came before is this summary:\n${entry.compaction}` }, entry.turn, range)
      } else {
        if (entry.text !== '') this.append({ case: 'user', text: entry.text }, entry.turn, range)
        if (entry.attachments.length > 0) this.append({ case: 'notice', text: `attached: ${Transcript.named(entry.attachments)}` }, entry.turn, range)
      }
      if (entry.tools.length > 0) {
        const activity: Activity = { reasoning: '', lines: entry.tools.map(Transcript.toolLine), live: false, budgetReached: false, typed: entry.compaction === undefined ? entry.text : undefined }
        this.append({ case: 'activity', activity }, entry.turn, range)
      }
      if (entry.answer !== '') this.append({ case: 'answer', answer: { text: entry.answer, finished: true, usage: undefined, model: undefined } }, entry.turn, range)
      if (!entry.answered && entry.compaction === undefined) this.append({ case: 'unanswered' }, entry.turn, range)
    }
    if (chat.turns.length > 0) this.append({ case: 'divider', text: Transcript.earlierDivider }, undefined, range)
  }

  /** A tool a turn called, as the live line names it, with its arguments and the size of what it returned. */
  static toolLine(call: SavedToolCall): string {
    let line = `[tool] ${call.name}`
    if (call.summary !== '') line += ` ${call.summary}`
    if (call.resultBytes !== undefined) line += ` -- returned ${Transcript.size(call.resultBytes)}`
    return line
  }

  /** Attachments by name, each with its files and size where the read says. */
  static named(attachments: readonly SavedAttachment[]): string {
    return attachments
      .map((attachment) => {
        const facts = [...(attachment.files === undefined ? [] : [`${attachment.files} ${attachment.files === 1 ? 'file' : 'files'}`]), ...(attachment.bytes === undefined ? [] : [Transcript.size(attachment.bytes)])]
        return facts.length === 0 ? attachment.name : `${attachment.name} (${facts.join(', ')})`
      })
      .join(', ')
  }

  /** Bytes as the Mac's `ByteCountFormatter` says a file's size. */
  static size(bytes: number): string {
    if (bytes < 1000) return `${bytes} bytes`
    const units = ['KB', 'MB', 'GB', 'TB']
    let value = bytes / 1000
    let unit = 0
    while (value >= 1000 && unit < units.length - 1) {
      value /= 1000
      unit += 1
    }
    return `${value >= 100 ? Math.round(value) : value.toFixed(1).replace(/\.0$/, '')} ${units[unit]}`
  }

  // MARK: - The session's events

  apply(event: SessionEvent): void {
    if (event.case === 'event') {
      this.applyEvent(event.event)
      return
    }
    // The child is gone, mid-answer or not: what it streamed is all there is.
    this.ahead = []
    this.flushRefused()
    this.closeActivity()
    this.closeStandingCards()
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

  /** What a turn recalled, counted, from a build before 52a that names no chat. */
  static recalledCount(chats: number, decisions: number): string {
    const count = (n: number, one: string, many: string) => `${n} ${n === 1 ? one : many}`
    const parts = [...(chats > 0 ? [count(chats, 'earlier chat', 'earlier chats')] : []), ...(decisions > 0 ? [count(decisions, 'decision', 'decisions')] : [])]
    return `recalled ${parts.length === 0 ? 'nothing' : parts.join(' and ')}`
  }

  private applyEvent(event: ProtocolEvent): void {
    const turn = event.turn
    if (turn !== undefined) this.atLaunch = false
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
        this.chatID = info.chatID ?? this.chatID
        this.schema = info.capabilities?.schema
        // The CLI's word on its suite (M29): a name, or -- from a binary that names its suite -- none.
        // A child being stopped for another says nothing of the suite the session moves to (design 1v).
        const namesSuite = (info.capabilities?.schema ?? '') >= MachineProtocol.namesSuite
        if (this.status.case !== 'restarting') {
          if (info.suite !== undefined) this.suite = { case: 'named', name: info.suite }
          else if (namesSuite) this.suite = { case: 'noSuite' }
          if (info.orchestrating !== undefined || namesSuite) this.orchestrating = info.orchestrating
        }
        this.spoke = true
        this.atLaunch = true
        this.launchNotices = []
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
      case 'memory': {
        const named = event.recalled
        if (named.length === 0) this.append({ case: 'notice', text: Transcript.recalledCount(kind.chats, kind.decisions) }, turn)
        else this.append({ case: 'recalled', recall: { chats: named, decisions: kind.decisions } }, turn)
        return
      }
      case 'sources':
        this.append({ case: 'sources', sources: kind.sources }, turn)
        return
      case 'notice':
        if (turn === undefined && this.atLaunch) {
          // Said at launch: a relaunch saying again what the child before it said at its own is drawn once.
          this.launchNotices.push(kind.text)
          if (this.earlierLaunch.includes(kind.text)) return
        }
        this.append({ case: 'notice', text: kind.text }, turn)
        return
      case 'todo': {
        // Drawn in place of the last: it is the plan, not a log.
        const index = this.items.findLastIndex((item) => item.content.case === 'todo')
        if (index >= 0) this.replace(index, () => ({ case: 'todo', items: kind.items }))
        else this.append({ case: 'todo', items: kind.items }, turn)
        return
      }
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
        // A card the turn left standing -- a stop while it asked -- can take no answer now.
        this.closeStandingCards()
        const answered = this.items.some((item) => item.content.case === 'answer' && this.isLive(item, turn))
        // A driver that dropped every delta still has the answer.
        if (!answered && result.text !== '') this.append({ case: 'answer', answer: { text: result.text, finished: true, usage: undefined, model: undefined } }, turn)
        this.finishAnswers(turn, result.usage, result.model === '' ? undefined : result.model)
        // The CLI's word for the model that answered: the label follows it.
        if (result.model !== undefined && result.model !== '') this.model = result.model
        if (result.finishReason === 'cancelled') this.append({ case: 'stopped' }, turn)
        else this.answeredTurns += 1
        // A restart under way keeps its state: the child is going.
        if (this.status.case !== 'restarting') this.status = { case: 'ready' }
        this.revision += 1
        return
      }
      case 'question':
        this.closeActivity()
        if (kind.question.kind === 'permission') this.append({ case: 'permission', card: { question: kind.question, answer: undefined, closed: false } }, turn)
        else this.append({ case: 'question', card: { question: kind.question, answers: undefined, closed: false } }, turn)
        if (this.status.case !== 'stopping' && this.status.case !== 'restarting' && this.status.case !== 'ended') this.status = { case: 'waiting', turn: turn ?? 0 }
        return
      case 'error':
        this.closeActivity()
        this.closeStandingCards()
        this.finishAnswers(turn, undefined, undefined)
        this.append({ case: 'error', message: kind.message, model: this.model }, turn)
        if (this.status.case !== 'restarting') this.status = { case: 'ready' }
        this.revision += 1
        return
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

  private append(content: TranscriptContent, turn: number | undefined, range: number = this.range): void {
    this.items = [...this.items, { id: this.nextID, turn, range, content }]
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

  /** Whether `item` is the live child's, of `turn`: what every live event is matched by -- its range and its turn, never the turn alone. */
  private isLive(item: TranscriptItem, turn: number | undefined): boolean {
    return item.range === this.range && item.turn === turn
  }

  /** Every card still standing is closed: its turn ended, or its child did, before it was answered. */
  private closeStandingCards(): void {
    this.items.forEach((item, index) => {
      const content = item.content
      if (content.case === 'question' && content.card.answers === undefined && !content.card.closed) this.replace(index, () => ({ case: 'question', card: { ...content.card, closed: true } }))
      if (content.case === 'permission' && content.card.answer === undefined && !content.card.closed) this.replace(index, () => ({ case: 'permission', card: { ...content.card, closed: true } }))
    })
  }

  /** The status, unless a `cancel` is on its way, the child is going, or it has gone. */
  private setStatus(next: TranscriptStatus): void {
    if (this.status.case === 'stopping' || this.status.case === 'ended' || this.status.case === 'restarting') return
    this.status = next
  }

  /**
   * The turn's activity: the last row, when it is this child's of this turn;
   * a new one otherwise, so the rows stay in the order things happened. The
   * sources and recall rows the turn drew between are looked past (design
   * 1v): the CLI says a turn's retrieval and recall after its query rewrite
   * and before its reasoning, and this port draws the sources after the
   * answer and the recall above the reply, as the old prototype does, so
   * neither splits the turn's activity in two.
   */
  private updateActivity(turn: number | undefined, change: (activity: Activity) => Activity): void {
    let last = this.items.length - 1
    const between = (item: TranscriptItem | undefined) => item !== undefined && (item.content.case === 'sources' || item.content.case === 'recalled') && this.isLive(item, turn)
    while (between(this.items[last])) last -= 1
    const item = this.items[last]
    if (item && item.content.case === 'activity' && this.isLive(item, turn)) {
      this.replace(last, (content) => (content.case === 'activity' ? { case: 'activity', activity: change(content.activity) } : content))
      return
    }
    const fresh: Activity = { reasoning: '', lines: [], live: true, budgetReached: false, typed: this.typed(turn) }
    this.append({ case: 'activity', activity: change(fresh) }, turn)
  }

  /** What the person sent as `turn`, to this child. */
  private typed(turn: number | undefined): string | undefined {
    if (turn === undefined) return undefined
    for (const item of [...this.items].reverse()) {
      if (this.isLive(item, turn) && item.content.case === 'user') return item.content.text
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
      if (item && this.isLive(item, turn) && item.content.case === 'answer' && !item.content.answer.finished) return index
    }
    return undefined
  }

  /** Every answer still streaming, in every range, finished: its child is gone, so nothing more will come of it. */
  private finishOpenAnswers(): void {
    this.items.forEach((item, index) => {
      if (item.content.case === 'answer' && !item.content.answer.finished) {
        this.replace(index, (content) => (content.case === 'answer' ? { case: 'answer', answer: { ...content.answer, finished: true } } : content))
      }
    })
  }

  private finishAnswers(turn: number | undefined, usage: Usage | undefined, model: string | undefined): void {
    this.items.forEach((item, index) => {
      if (!this.isLive(item, turn) || item.content.case !== 'answer') return
      this.replace(index, (content) =>
        content.case === 'answer'
          ? { case: 'answer', answer: { ...content.answer, finished: true, usage: usage ?? content.answer.usage, model: model ?? content.answer.model } }
          : content,
      )
    })
  }
}
