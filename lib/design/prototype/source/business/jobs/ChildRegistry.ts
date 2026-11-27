/** What holds a child: the session's chat, or a supervised job's run. */
export type ChildKind = 'session' | 'job'

/** A child, as the places that read the list see it. */
export interface Child {
  /** Its place in the registry, in the order children registered. */
  readonly id: number
  readonly kind: ChildKind
  /** What it is, in a few words: `apogee models pull Qwen/Qwen3-8B`. */
  readonly label: string
  /** The arguments it runs, after `apogee`, when its supervisor gave them (design 1k): what another place reads of it -- a generate's `-m`. */
  readonly arguments?: readonly string[]
}

/** One child's place, held by its supervisor: renamed as it learns more, left at its end. */
export interface Registration {
  readonly id: number
  relabel(label: string): void
  /** It ended: it is no longer listed. Leaving twice is leaving once. */
  leave(): void
}

interface Entry {
  child: Child
  readonly stop: () => Promise<void>
}

/**
 * Every child the app runs and supervises, in one place (design 1h): the
 * Mac app's `ChildRegistry` (`BusinessJobs`, `ChildRegistry.swift`). A child
 * is registered by the supervisor that holds it, never by a screen -- the
 * job starter when it starts a run -- and leaves when its end is said. So a
 * run any place starts is seen by every other -- the sidebar's running
 * counts, the monitor -- without importing the place that started it.
 * Swift's `Mutex` has no twin here; whoever watches is told each change
 * (`onChange`), as Observation would tell a view.
 */
export class ChildRegistry {
  /** The longest label a job is given. */
  static readonly labelLimit = 120

  private next = 1
  private entries: Entry[] = []
  private readonly watchers = new Set<() => void>()

  /** Lists a child that has just started; `stop` is its supervisor's Stop. */
  register(kind: ChildKind, label: string, stop: () => Promise<void>, args?: readonly string[]): Registration {
    const id = this.next
    this.next += 1
    this.entries.push({ child: args === undefined ? { id, kind, label } : { id, kind, label, arguments: [...args] }, stop })
    this.tell()
    return {
      id,
      relabel: (label) => {
        const entry = this.entries.find((candidate) => candidate.child.id === id)
        if (!entry) return
        entry.child = { ...entry.child, label }
        this.tell()
      },
      leave: () => {
        const before = this.entries.length
        this.entries = this.entries.filter((candidate) => candidate.child.id !== id)
        if (this.entries.length !== before) this.tell()
      },
    }
  }

  /** Every child running now, or those of one kind, in the order they started. */
  children(kind?: ChildKind): Child[] {
    return this.entries.filter((entry) => kind === undefined || entry.child.kind === kind).map((entry) => entry.child)
  }

  /** Stops every child of `kind` running now, each by its own supervisor's Stop; whether all of them have left. */
  async stop(kind: ChildKind): Promise<boolean> {
    const stopping = this.entries.filter((entry) => entry.child.kind === kind)
    await Promise.all(stopping.map((entry) => entry.stop()))
    const ids = new Set(stopping.map((entry) => entry.child.id))
    return !this.entries.some((entry) => ids.has(entry.child.id))
  }

  /** Stops the one child `id`, by its supervisor's Stop (design 1k: the monitor's Stop on a run); whether it was running and has left. */
  async stopChild(id: number): Promise<boolean> {
    const entry = this.entries.find((candidate) => candidate.child.id === id)
    if (!entry) return false
    await entry.stop()
    return !this.entries.some((candidate) => candidate.child.id === id)
  }

  /** Tells `watcher` of every change from now on; the returned function stops it. */
  onChange(watcher: () => void): () => void {
    this.watchers.add(watcher)
    return () => this.watchers.delete(watcher)
  }

  /**
   * A job's label: the command as a person would type it -- `apogee` and the
   * words before its first option, then a person's text after `--` -- the
   * first three arguments when it opens with an option, and no more than
   * `labelLimit` characters.
   */
  static label(args: readonly string[]): string {
    const words: string[] = []
    for (const word of args) {
      if (word.startsWith('-')) break
      words.push(word)
    }
    const dashes = args.indexOf('--')
    const text = dashes >= 0 ? args.slice(dashes + 1) : []
    const said = ['apogee', ...(words.length === 0 ? args.slice(0, 3) : [...words, ...text])].join(' ')
    return said.length > ChildRegistry.labelLimit ? `${said.slice(0, ChildRegistry.labelLimit - 1)}…` : said
  }

  private tell(): void {
    for (const watcher of this.watchers) watcher()
  }
}
