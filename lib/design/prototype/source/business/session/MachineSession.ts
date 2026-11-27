import { DriverLine, RunSettings } from '../../data/protocol/DriverLine'
import { EventDecoder } from '../../data/protocol/EventDecoder'
import { MachineProtocol } from '../../data/protocol/MachineProtocol'
import type { Clock } from '../../infrastructure/clock/Clock'
import { AsyncStream } from '../../infrastructure/process/AsyncStream'
import type { Continuation } from '../../infrastructure/process/AsyncStream'
import { BoundedTail } from '../../infrastructure/process/BoundedTail'
import { ChildExit } from '../../infrastructure/process/ChildExit'
import { LineFramer } from '../../infrastructure/process/LineFramer'
import type { CapturedRun, ReplayRules, StandInChild } from '../../infrastructure/process/StandInChild'
import { ChatLaunch } from './ChatLaunch'
import { SessionEnd, SessionError } from './ChatSession'
import type { ChatSession, SessionEndReason, SessionEvent } from './ChatSession'

/**
 * A chat session, as the Mac app's `MachineSession` says one (`BusinessSession`,
 * `MachineSession.swift`), over a recorded machine-mode stream (design 1d).
 *
 * The recording is cut where the binary would have waited for input: the
 * opening (the `session` line, and the lines with no turn before the first
 * turn's), then one turn per `send` -- a turn's lines, and any line with no
 * turn after them. Lines play one a tick (or the preset's pace), and a turn
 * waits at each `question` until `answer` has answered its every item (an
 * answer written ahead is read when its question asks, as the binary reads
 * its stdin), or until a cancel when the recording ends the turn right there.
 * Every line written is kept in `written`, as the binary would read it; the
 * replay goes on as recorded. A child whose rule names a non-zero exit ends
 * on its own once its lines are out; otherwise it waits for input, and its
 * input closing ends it.
 *
 * Its rule's replay rules (design 1v) move it further: a still frame stands
 * at its freeze, a named event, and a live clock plays straight to it; a
 * closed child exits `exitAfter` ticks later, taking no line meanwhile while
 * what it had written drains; and
 * at a choice point the first answer, turn or setting written plays the
 * branch its rule names for it, from the line the replay has reached; and a
 * cancel ends the open turn as the rule's `cancelled` recording ended it.
 */
export class MachineSession implements ChatSession {
  readonly events: AsyncStream<SessionEvent>
  /** Every line written to the session, encoded as the binary reads it. */
  readonly written: string[] = []
  private readonly continuation: Continuation<SessionEvent>
  /** The recording's turns, as cut; the opening is queued at once. */
  private turns: string[][] = []
  private queued: string[] = []
  /** The turns sent, so released into the queue. */
  private released = 0
  /** The recording's lines played so far, from its first: where a branch takes over. */
  private played = 0
  /** How many lines of each type have played: what a named event is counted by. */
  private readonly playedOf = new Map<string, number>()
  private answersOwed = 0
  /** Answers written before their question played: the binary reads them when it asks. */
  private answersAhead = 0
  private nextTurn = 1
  /** Turns sent and not yet ended in their `result` or `error`, counted from the protocol's events as `TurnTracker` counts them. */
  private turnsOpen = 0
  private inputClosed = false
  /** Ticks left before a child whose input closed exits; `undefined` while it reads. */
  private exitIn: number | undefined = undefined
  /** The replay has played its freeze's event. */
  private frozen = false
  /** The choice point's line has been written. */
  private chosen = false
  /** Whether the clock stands still: a freeze holds only there. */
  private still = false
  private leave: () => void = () => {}

  private constructor(
    private readonly child: StandInChild | undefined,
    private readonly run: CapturedRun | undefined,
    private readonly replay: ReplayRules | undefined,
    private readonly launchLine: string,
  ) {
    const { stream, continuation } = AsyncStream.make<SessionEvent>()
    this.events = stream
    this.continuation = continuation
    if (!run) return
    const cut = MachineSession.cut(LineFramer.lines(run.stdout))
    this.queued = cut.opening
    this.turns = cut.turns
  }

  /** A recording cut where the binary waited: its opening, then one turn per send, a line with no turn going with the turn before it. */
  private static cut(lines: readonly string[]): { opening: string[]; turns: string[][] } {
    let index = 0
    const opening: string[] = []
    while (index < lines.length && EventDecoder.decode(lines[index] ?? '').turn === undefined) {
      opening.push(lines[index] ?? '')
      index += 1
    }
    const turns: string[][] = []
    let current: string[] = []
    let currentTurn: number | undefined
    for (const line of lines.slice(index)) {
      const turn = EventDecoder.decode(line).turn
      if (turn !== undefined && turn !== currentTurn) {
        current = []
        turns.push(current)
        currentTurn = turn
      }
      current.push(line)
    }
    return { opening, turns }
  }

  /**
   * The chat `launch` opens: its rule's recording replayed on `clock`, or its
   * end at once when it never starts. In a still frame it opens as many ticks
   * in as the frame, however late -- its opening only, since no send is
   * written yet.
   */
  static open(child: StandInChild, clock: Clock, launch: ChatLaunch = {}): MachineSession {
    const args = ChatLaunch.arguments(launch)
    const answer = child.run('chats', args)
    const launchLine = `apogee ${args.join(' ')}`
    if (!answer.started) {
      const session = new MachineSession(undefined, undefined, undefined, launchLine)
      session.end({ case: 'couldNotStart' }, undefined, new BoundedTail(), answer.reason)
      return session
    }
    const session = new MachineSession(child, answer.run, answer.replay, launchLine)
    session.still = clock.still
    session.leave = clock.onTick(() => session.step(clock.linesPerTick))
    // After `leave` is set, so a recording that ends within the frame's ticks leaves once.
    for (let tick = 0; tick < clock.missedTicks && !session.continuation.finished; tick += 1) session.step(clock.linesPerTick)
    return session
  }

  async send(text: string): Promise<number> {
    this.write({ case: 'user', text })
    const turn = this.nextTurn
    this.nextTurn += 1
    // A sent turn is in flight whether or not a recording answers it, as `TurnTracker.sent()` counts it.
    this.turnsOpen += 1
    const lines = this.turns[this.released]
    if (lines) {
      this.released += 1
      this.queued.push(...lines)
    } else if (this.run && ChildExit.succeeded(this.run.exit)) {
      // A child that refused its launch exits without reading its input
      // (machine-mode.md -> A launch refused): no recording is owed for a
      // turn it never took (design 1u). Any other send past the recording is.
      this.child?.note('chats', `turn ${turn} of ${this.launchLine}`)
    }
    return turn
  }

  async answer(text: string): Promise<void> {
    this.write({ case: 'answer', text })
    if (this.answersOwed > 0) this.answersOwed -= 1
    else this.answersAhead += 1
  }

  async attach(path: string, graph?: string): Promise<void> {
    this.write(graph === undefined ? { case: 'attach', path } : { case: 'attach', path, graph })
  }

  async detach(path: string): Promise<void> {
    this.write({ case: 'detach', path })
  }

  async set(settings: RunSettings): Promise<void> {
    this.write({ case: 'set', settings })
  }

  async compact(): Promise<void> {
    this.write({ case: 'compact' })
  }

  async cancel(): Promise<void> {
    this.write({ case: 'cancel' })
    // With no turn open the binary ignores it (machine-mode.md, Stopping a turn).
    if (this.turnsOpen > 0) this.endTurnCancelled()
  }

  /**
   * The open turn ended by a cancel, as the binary ends one: where the
   * recording itself ends the turn here -- its `result` next, past only the
   * calls it reports -- that end plays (`question-cancelled.jsonl`); else the
   * turn's unplayed lines are dropped and the rule's `cancelled` recording's
   * ending of the same turn takes their place. A cancel nothing recorded is
   * noted, and the replay goes on as recorded.
   */
  private endTurnCancelled(): void {
    const decoded = this.queued.map((line) => EventDecoder.decode(line))
    const ends = decoded.findIndex((event) => event.kind.case === 'result' || event.kind.case === 'error')
    const asks = decoded.findIndex((event) => event.kind.case === 'question')
    if (ends >= 0 && (asks < 0 || ends < asks) && decoded.slice(0, ends).every((event) => event.kind.case === 'toolCall')) {
      this.answersOwed = 0
      return
    }
    // The turn of the last turn sent that has not ended, as the binary's cancel reaches it.
    const turn = decoded.reduce<number | undefined>((last, event) => (event.turn === undefined ? last : Math.max(last ?? event.turn, event.turn)), undefined)
    const ending = turn === undefined ? undefined : MachineSession.cancelledEnding(this.replay?.cancelled, turn)
    if (turn === undefined || ending === undefined) {
      this.child?.note('chats', `cancel during turn ${turn ?? this.nextTurn - 1} of ${this.launchLine}`)
      return
    }
    const first = decoded.findIndex((event) => event.turn === turn)
    const kept = this.queued.filter((_, index) => decoded[index]?.turn !== turn)
    this.queued = [...kept.slice(0, first), ...ending, ...kept.slice(first)]
    this.answersOwed = 0
    // The question it answered was the choice point's: nothing more is chosen there.
    const choices = this.replay?.continuations
    if (choices && (this.playedOf.get(choices.after) ?? 0) >= (choices.nth ?? 1)) this.chosen = true
  }

  /** How `recording` ended `turn` on a cancel: its `result` with `finish_reason: "cancelled"`, and the calls reported `failed` just before it; `undefined` where it ended none. */
  private static cancelledEnding(recording: string | undefined, turn: number): string[] | undefined {
    if (recording === undefined) return undefined
    const lines = LineFramer.lines(recording)
    const decoded = lines.map((line) => EventDecoder.decode(line))
    const at = decoded.findLastIndex((event) => event.turn === turn && event.kind.case === 'result' && event.kind.result.finishReason === 'cancelled')
    if (at < 0) return undefined
    let from = at
    while (from > 0) {
      const before = decoded[from - 1]?.kind
      if (before?.case !== 'toolCall' || before.call.state !== 'failed' || decoded[from - 1]?.turn !== turn) break
      from -= 1
    }
    return lines.slice(from, at + 1)
  }

  /** Its input closes: the child saves and exits -- at once, or as many ticks later as its rule's `exitAfter` -- finished when its exit is clean. */
  async close(): Promise<void> {
    if (this.inputClosed || this.continuation.finished || !this.run) return
    this.inputClosed = true
    const after = this.replay?.exitAfter ?? 0
    if (after > 0) this.exitIn = after
    else this.exit()
  }

  private exit(): void {
    if (!this.run) return
    const exit = this.run.exit
    this.end(ChildExit.succeeded(exit) ? { case: 'finished' } : { case: 'failed' }, exit, this.run.stderrTail)
  }

  /** A turn in flight is cancelled, then the input closed. */
  async stop(_grace?: number): Promise<void> {
    if (this.continuation.finished) return
    if (this.turnsOpen > 0 && !this.inputClosed) this.write({ case: 'cancel' })
    await this.close()
  }

  /** The stop's terminate step, now: its end is a stop. */
  async terminateNow(): Promise<void> {
    if (this.continuation.finished || !this.run) return
    this.inputClosed = true
    this.end({ case: 'stopped' }, ChildExit.signalled(15), this.run.stderrTail)
  }

  private write(line: DriverLine): void {
    if (this.continuation.finished || this.inputClosed) throw new SessionError()
    this.written.push(DriverLine.encoded(line))
    this.choose(line)
  }

  /**
   * What a written line is called at a choice point: an answer by its text, a
   * turn as `user`, a setting by what it sets (`set model=claude`, each field
   * as the line writes it, a text unquoted); `undefined` for a line that is no
   * choice.
   */
  private static choiceOf(line: DriverLine): string | undefined {
    switch (line.case) {
      case 'answer':
        return line.text
      case 'user':
        return line.case
      case 'set':
        return ['set', ...RunSettings.fields(line.settings).map(([key, value]) => `${key}=${value.startsWith('"') ? (JSON.parse(value) as string) : value}`)].join(' ')
      default:
        return undefined
    }
  }

  /** At the choice point, the first choice written plays its branch from the line the replay has reached; one no branch names is noted. */
  private choose(line: DriverLine): void {
    const choices = this.replay?.continuations
    const key = MachineSession.choiceOf(line)
    if (!choices || this.chosen || key === undefined) return
    if ((this.playedOf.get(choices.after) ?? 0) < (choices.nth ?? 1)) return
    this.chosen = true
    const branch = choices.by[key]
    if (branch === undefined) {
      this.child?.note('chats', `${line.case === 'answer' ? `answer ${key}` : key} after the ${choices.after} of ${this.launchLine}`)
      return
    }
    // The branch shares the recording's lines up to here: what the turns sent so far released of it, past the lines played, is queued.
    const cut = MachineSession.cut(LineFramer.lines(branch))
    const released = [...cut.opening, ...cut.turns.slice(0, this.released).flat()]
    this.queued = released.slice(this.played)
    this.turns = cut.turns
  }

  private step(pace: number): void {
    if (this.continuation.finished || !this.run) return
    if (this.exitIn !== undefined) {
      // Its input closed: it takes no line; what it had written drains, one a tick, until it exits.
      if (this.queued.length > 0 && this.answersOwed === 0) this.playNext()
      this.exitIn -= 1
      if (this.exitIn <= 0) this.exit()
      return
    }
    if (this.answersOwed > 0) return
    if (this.queued.length === 0) {
      // A child that exits on its own -- a refused launch, a crash -- ends once its every line is out.
      if (!ChildExit.succeeded(this.run.exit) && this.released === this.turns.length) {
        this.end({ case: 'failed' }, this.run.exit, this.run.stderrTail)
      }
      return
    }
    const freeze = this.replay?.freeze
    if (freeze && this.frozen && this.still) return
    if (freeze && !this.frozen) {
      // Straight to the named event, however many lines that is; a still frame stands there.
      while (!this.frozen && this.queued.length > 0 && this.answersOwed === 0 && !this.continuation.finished) {
        this.playNext()
        this.frozen = (this.playedOf.get(freeze.event) ?? 0) >= freeze.nth
      }
      return
    }
    for (let played = 0; played < pace && this.queued.length > 0 && this.answersOwed === 0 && !this.continuation.finished; played += 1) this.playNext()
  }

  /** The next queued line, played. */
  private playNext(): void {
    if (!this.run) return
    const event = EventDecoder.decode(this.queued.shift() ?? '')
    this.played += 1
    this.playedOf.set(event.type, (this.playedOf.get(event.type) ?? 0) + 1)
    if (event.kind.case === 'session') {
      const { protocolVersion, nextTurn } = event.kind.session
      if (!MachineProtocol.understands(protocolVersion)) {
        // An existing event's meaning has changed: refuse the stream rather than misread it.
        this.end({ case: 'unsupportedProtocol', version: protocolVersion }, ChildExit.signalled(15), this.run.stderrTail)
        return
      }
      this.nextTurn = nextTurn + this.released
    }
    this.continuation.yield({ case: 'event', event })
    if (event.kind.case === 'result' || event.kind.case === 'error') this.turnsOpen = Math.max(0, this.turnsOpen - 1)
    if (event.kind.case === 'question') {
      const asked = Math.max(1, event.kind.question.items.length)
      const answered = Math.min(asked, this.answersAhead)
      this.answersAhead -= answered
      this.answersOwed = asked - answered
    }
  }

  private end(reason: SessionEndReason, exit: ChildExit | undefined, stderrTail: BoundedTail, detail?: string): void {
    this.leave()
    this.continuation.yield({ case: 'ended', end: SessionEnd.of(reason, exit, stderrTail, detail) })
    this.continuation.finish()
  }
}
