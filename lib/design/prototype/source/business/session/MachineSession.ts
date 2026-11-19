import { DriverLine } from '../../data/protocol/DriverLine'
import type { RunSettings } from '../../data/protocol/DriverLine'
import { EventDecoder } from '../../data/protocol/EventDecoder'
import { MachineProtocol } from '../../data/protocol/MachineProtocol'
import type { Clock } from '../../infrastructure/clock/Clock'
import { AsyncStream } from '../../infrastructure/process/AsyncStream'
import type { Continuation } from '../../infrastructure/process/AsyncStream'
import { BoundedTail } from '../../infrastructure/process/BoundedTail'
import { ChildExit } from '../../infrastructure/process/ChildExit'
import { LineFramer } from '../../infrastructure/process/LineFramer'
import type { CapturedRun, StandInChild } from '../../infrastructure/process/StandInChild'
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
 */
export class MachineSession implements ChatSession {
  readonly events: AsyncStream<SessionEvent>
  /** Every line written to the session, encoded as the binary reads it. */
  readonly written: string[] = []
  private readonly continuation: Continuation<SessionEvent>
  private readonly turns: string[][] = []
  private readonly queued: string[] = []
  private released = 0
  private answersOwed = 0
  /** Answers written before their question played: the binary reads them when it asks. */
  private answersAhead = 0
  private nextTurn = 1
  /** Turns sent and not yet ended in their `result` or `error`, counted from the protocol's events as `TurnTracker` counts them. */
  private turnsOpen = 0
  private inputClosed = false
  private leave: () => void = () => {}

  private constructor(
    private readonly child: StandInChild | undefined,
    private readonly run: CapturedRun | undefined,
    private readonly launchLine: string,
  ) {
    const { stream, continuation } = AsyncStream.make<SessionEvent>()
    this.events = stream
    this.continuation = continuation
    if (!run) return
    const lines = LineFramer.lines(run.stdout)
    let index = 0
    while (index < lines.length && EventDecoder.decode(lines[index] ?? '').turn === undefined) {
      this.queued.push(lines[index] ?? '')
      index += 1
    }
    let current: string[] = []
    let currentTurn: number | undefined
    for (const line of lines.slice(index)) {
      const turn = EventDecoder.decode(line).turn
      if (turn !== undefined && turn !== currentTurn) {
        current = []
        this.turns.push(current)
        currentTurn = turn
      }
      current.push(line)
    }
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
      const session = new MachineSession(undefined, undefined, launchLine)
      session.end({ case: 'couldNotStart' }, undefined, new BoundedTail(), answer.reason)
      return session
    }
    const session = new MachineSession(child, answer.run, launchLine)
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
    } else {
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
    // A cancel at a standing question ends the wait when the recording ends the turn there, as the binary did.
    const next = this.queued[0]
    if (this.answersOwed > 0 && next !== undefined && EventDecoder.decode(next).kind.case === 'result') this.answersOwed = 0
  }

  /** Its input closes: the child saves and exits, finished when its exit is clean. */
  async close(): Promise<void> {
    if (this.inputClosed || this.continuation.finished || !this.run) return
    this.inputClosed = true
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
  }

  private step(pace: number): void {
    if (this.continuation.finished || !this.run || this.answersOwed > 0) return
    if (this.queued.length === 0) {
      // A child that exits on its own -- a refused launch, a crash -- ends once its every line is out.
      if (!ChildExit.succeeded(this.run.exit) && this.released === this.turns.length) {
        this.end({ case: 'failed' }, this.run.exit, this.run.stderrTail)
      }
      return
    }
    for (let played = 0; played < pace && this.queued.length > 0; played += 1) {
      const event = EventDecoder.decode(this.queued.shift() ?? '')
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
        return
      }
    }
  }

  private end(reason: SessionEndReason, exit: ChildExit | undefined, stderrTail: BoundedTail, detail?: string): void {
    this.leave()
    this.continuation.yield({ case: 'ended', end: SessionEnd.of(reason, exit, stderrTail, detail) })
    this.continuation.finish()
  }
}
