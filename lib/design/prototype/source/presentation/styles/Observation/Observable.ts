/**
 * What a view-model's view watches (design 1e): Swift's Observation, which
 * TypeScript lacks, in the smallest form React's `useSyncExternalStore`
 * reads -- a subscription, and a snapshot that moves on every change. A
 * view-model extends it and calls `changed()` after it changes. Plain
 * TypeScript, no React, so a view-model tests without a browser. It sits in
 * `styles`, the one module every Presentation module may use, as 1d put
 * `AsyncStream`, Swift's stream, beside the stand-in.
 */
export class Observable {
  private readonly listeners = new Set<() => void>()
  private revision = 0

  /** Calls `listener` after each change; returns what stops it. */
  readonly subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener)
    return () => {
      this.listeners.delete(listener)
    }
  }

  /** How many changes so far: what a view compares to know it must draw again. */
  readonly snapshot = (): number => this.revision

  /** Tells every listener the view-model changed. */
  protected changed(): void {
    this.revision += 1
    for (const listener of [...this.listeners]) listener()
  }
}
