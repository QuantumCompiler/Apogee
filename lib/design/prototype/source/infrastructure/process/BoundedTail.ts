const encoder = new TextEncoder()
const decoder = new TextDecoder()

/**
 * The last bytes a stream carried, and no more: the Mac app's `BoundedTail`
 * (`InfrastructureProcess`, `BoundedTail.swift`). A child's stderr is kept to
 * its last `limit` bytes and folded into a failure when the child fails.
 */
export class BoundedTail {
  private bytes = new Uint8Array(0)

  constructor(readonly limit = 8 * 1024) {}

  /** A tail holding `text`, as a child that wrote it leaves one. */
  static of(text: string): BoundedTail {
    const tail = new BoundedTail()
    tail.append(text)
    return tail
  }

  append(chunk: string): void {
    const added = encoder.encode(chunk)
    const joined = new Uint8Array(this.bytes.length + added.length)
    joined.set(this.bytes)
    joined.set(added, this.bytes.length)
    this.bytes = joined.length > this.limit ? joined.slice(joined.length - this.limit) : joined
  }

  /** Everything kept, as text. */
  get text(): string {
    return decoder.decode(this.bytes)
  }

  /** The last line with anything on it, trimmed: what a one-line failure says. */
  get lastLine(): string | undefined {
    return this.text
      .split(/\r\n|\r|\n/)
      .map((line) => line.trim())
      .filter((line) => line !== '')
      .at(-1)
  }

  get isEmpty(): boolean {
    return this.bytes.length === 0
  }
}
