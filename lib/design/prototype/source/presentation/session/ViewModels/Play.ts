/** A symphony `/play` can name, as `symphonies list` says it: the Mac app's `PlayChoice`. */
export interface PlayChoice {
  readonly name: string
  readonly description: string
}

/** One stage as its line said it: `stage 1/2 first — asking utility (helper): One: hello`. */
export interface PlayStage {
  readonly index: number
  readonly count: number
  readonly name: string
  readonly role: string
  readonly backend: string
  /** Where a stage of a symphony another plays sits: `digest → summarize-verify`. */
  readonly chain: string | undefined
  /** The line, whole. */
  readonly line: string
}

/**
 * A symphony played in a turn, as the session's own lines say it
 * (machine-mode.md -> Playing a symphony): the Mac app's `PlayNarration`
 * (`Play.swift`). Nothing new crosses the wire: these are `tool_status`
 * lines, each kept as the CLI wrote it and read here only to say which stage
 * the play is on.
 */
export class PlayNarration {
  private constructor(
    /** The symphony, as the model's choice or the typed `/play` named it. */
    readonly symphony: string | undefined,
    /** The model chose it (`--orchestrate`): its line, whole. */
    readonly chosen: string | undefined,
    readonly stages: readonly PlayStage[],
  ) {}

  /** The play `lines` narrate, for a turn typed as `typed`; `undefined` when they narrate none. */
  static of(lines: readonly string[], typed: string | undefined): PlayNarration | undefined {
    let chosen: string | undefined
    let symphony: string | undefined
    const stages: PlayStage[] = []
    for (const line of lines) {
      const choice = /^play — the model chose (\S+?):/.exec(line)
      if (choice) {
        chosen = line
        symphony = choice[1]
        continue
      }
      const stage = PlayNarration.stage(line)
      if (stage) stages.push(stage)
    }
    if (chosen === undefined && stages.length === 0) return undefined
    if (symphony === undefined && typed !== undefined) symphony = /^\/play\s+(\S+)/.exec(typed)?.[1]
    return new PlayNarration(symphony, chosen, stages)
  }

  /** The stage playing now, or played last. */
  get current(): PlayStage | undefined {
    return this.stages.at(-1)
  }

  /** What the play is doing, in a few words: `playing echo2 · stage 2 of 2`, `played echo2 · 2 stages`. */
  summary(live: boolean): string {
    const name = this.symphony === undefined ? '' : ` ${this.symphony}`
    const current = this.current
    if (!current) return (live ? 'playing' : 'played') + name
    if (live) return `playing${name} · stage ${current.index} of ${current.count}`
    const count = this.stages.length
    return `played${name} · ${count} ${count === 1 ? 'stage' : 'stages'}`
  }

  /** A line's stage, when it is one. */
  static stage(line: string): PlayStage | undefined {
    const match = /^(?:(.+), )?stage (\d+)\/(\d+) (\S+) — asking (\S+) \(([^)]*)\)/.exec(line)
    if (!match) return undefined
    return {
      index: Number(match[2]),
      count: Number(match[3]),
      name: match[4] ?? '',
      role: match[5] ?? '',
      backend: match[6] ?? '',
      chain: match[1],
      line,
    }
  }
}
