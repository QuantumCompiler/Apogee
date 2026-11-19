/** The writing end of a stream: what its maker yields into, then finishes. */
export interface Continuation<T> {
  yield(value: T): void
  /** Ends the stream: a reader's loop ends once it has read what was yielded. Later yields are dropped. */
  finish(): void
  readonly finished: boolean
}

/**
 * Values in order, read with `for await`: Swift's `AsyncStream`, which the Mac
 * app's children, jobs and sessions say what they say through
 * (`RunningChild.lines`, `JobRunning.updates`, `ChatSession.events`).
 * TypeScript has none, so the process module, where a child's output starts,
 * holds this one. A value goes to one reader, as Swift's does.
 */
export class AsyncStream<T> implements AsyncIterable<T> {
  private readonly queue: T[] = []
  private readonly waiting: Array<(result: IteratorResult<T>) => void> = []
  private readonly history: T[] = []
  private ended = false

  private constructor() {}

  /** A stream and the continuation that writes it. */
  static make<T>(): { stream: AsyncStream<T>; continuation: Continuation<T> } {
    const stream = new AsyncStream<T>()
    const continuation: Continuation<T> = {
      yield: (value) => stream.push(value),
      finish: () => stream.end(),
      get finished() {
        return stream.ended
      },
    }
    return { stream, continuation }
  }

  /** Everything yielded so far, read or not: what a still frame shows, and what a test checks. */
  get yielded(): readonly T[] {
    return this.history
  }

  [Symbol.asyncIterator](): AsyncIterator<T> {
    return {
      next: () => {
        if (this.queue.length > 0) return Promise.resolve({ value: this.queue.shift() as T, done: false })
        if (this.ended) return Promise.resolve({ value: undefined, done: true })
        return new Promise((resolve) => this.waiting.push(resolve))
      },
    }
  }

  private push(value: T): void {
    if (this.ended) return
    this.history.push(value)
    const reader = this.waiting.shift()
    if (reader) reader({ value, done: false })
    else this.queue.push(value)
  }

  private end(): void {
    if (this.ended) return
    this.ended = true
    for (const reader of this.waiting.splice(0)) reader({ value: undefined, done: true })
  }
}
