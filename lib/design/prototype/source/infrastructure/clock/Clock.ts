/**
 * A preset's clock: where it starts, whether it stands still, its fixed "now",
 * and how fast a job or chat replays (design 1d).
 */
export interface ClockSettings {
  /** Ticks applied at once when the clock starts. 0 unless the preset says. */
  readonly start?: number
  /** Applies the start ticks, then never ticks again: a frame that does not move. */
  readonly still?: boolean
  /** "Now", fixed for the preset, as an ISO 8601 time with its zone (Z or ±hh:mm). `Clock.defaultNow` unless the preset names another. */
  readonly now?: string
  /** Recorded lines a job or chat replays each tick. 1 unless the preset sets a faster pace. */
  readonly pace?: number
}

/** What schedules the ticks: the page's own timers, or a test's fakes of them. */
export interface Timers {
  setInterval(callback: () => void, milliseconds: number): unknown
  clearInterval(handle: unknown): void
}

const pageTimers: Timers = {
  setInterval: (callback, milliseconds) => globalThis.setInterval(callback, milliseconds),
  clearInterval: (handle) => globalThis.clearInterval(handle as ReturnType<typeof globalThis.setInterval>),
}

/**
 * The prototype's clock: the old prototype's `tick()`, every 400 ms unless the
 * page is `still` (the old prototype's `main.html:2849-2850` at `b375fac9`).
 * Jobs and chats replay their recorded lines on it. It has no Mac module: the
 * Mac app's children run on real time.
 *
 * "Now" is fixed per preset, so "3 minutes ago" never drifts in a screenshot.
 * Tests drive it by hand with `advance(n)`.
 */
export class Clock {
  static readonly tickMilliseconds = 400
  static readonly defaultNow = '2026-11-14T12:00:00Z'

  readonly still: boolean
  /** The ticks a start applies at once. */
  readonly startTicks: number
  /** Recorded lines a job or chat replays each tick. */
  readonly linesPerTick: number
  private readonly nowMilliseconds: number
  private readonly listeners: Array<(tick: number) => void> = []
  private ticked = 0
  private started = false
  private handle: unknown = undefined

  constructor(
    settings: ClockSettings = {},
    private readonly timers: Timers = pageTimers,
  ) {
    const start = settings.start ?? 0
    if (!Number.isInteger(start) || start < 0) {
      throw new Error(`a preset's start is a whole number of ticks, 0 or more, not ${start}`)
    }
    const pace = settings.pace ?? 1
    if (!Number.isInteger(pace) || pace < 1) {
      throw new Error(`a preset's pace is a whole number of lines a tick, 1 or more, not ${pace}`)
    }
    const now = settings.now ?? Clock.defaultNow
    // Its zone is required: a time without one would parse in the viewer's own zone, and "now" would differ by machine.
    const milliseconds = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2}(\.\d+)?)?(Z|[+-]\d{2}:\d{2})$/.test(now) ? Date.parse(now) : Number.NaN
    if (Number.isNaN(milliseconds)) throw new Error(`a preset's "now" is an ISO 8601 time, not ${now}`)
    this.still = settings.still ?? false
    this.startTicks = start
    this.linesPerTick = pace
    this.nowMilliseconds = milliseconds
  }

  /** The ticks so far. */
  get ticks(): number {
    return this.ticked
  }

  /**
   * The ticks a job or chat that opens now has missed: a still frame is its
   * ticks in for everything it shows, whenever that thing opened -- its start
   * ticks, once started (and any a test advanced by hand). A live or unstarted
   * clock owes none: what opens late starts at its first line.
   */
  get missedTicks(): number {
    return this.started && this.still ? this.ticked : 0
  }

  /** The preset's fixed "now": the same instant on every read. */
  get now(): Date {
    return new Date(this.nowMilliseconds)
  }

  /** Calls `listener` with each tick's number, from the next tick on; returns what stops it. */
  onTick(listener: (tick: number) => void): () => void {
    this.listeners.push(listener)
    return () => {
      const index = this.listeners.indexOf(listener)
      if (index >= 0) this.listeners.splice(index, 1)
    }
  }

  /** Runs `count` ticks now, each listener in the order it joined. */
  advance(count = 1): void {
    for (let step = 0; step < count; step += 1) {
      this.ticked += 1
      for (const listener of [...this.listeners]) listener(this.ticked)
    }
  }

  /**
   * Applies the start ticks at once -- the first time only -- then, unless
   * `still`, ticks every 400 ms until `stop`.
   */
  start(): void {
    if (!this.started) {
      this.started = true
      this.advance(this.startTicks)
    }
    if (this.still || this.handle !== undefined) return
    this.handle = this.timers.setInterval(() => this.advance(), Clock.tickMilliseconds)
  }

  stop(): void {
    if (this.handle === undefined) return
    this.timers.clearInterval(this.handle)
    this.handle = undefined
  }
}
