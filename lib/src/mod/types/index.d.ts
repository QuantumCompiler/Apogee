// The mod's contract: the values it keeps in $.state, declared once.
// plugin.json names this file as "types"; every layer imports its types from
// here (type-only), and it imports nothing itself.

/** Where a candidate binary came from: the person's override or a channel's place. */
export type ApogeeSource = 'override' | 'release' | 'dev' | 'test'

/** A place discovery looked and passed over, and why. */
export type ApogeeSkipped = {
  /** Where it looked, as the person would type it (`~/.local/bin/apogee`). */
  path: string
  source: ApogeeSource
  /** Why it was passed over: `not installed`, why it could not be looked at, or what `version` did. */
  reason: string
}

/** What discovery left: the binary that answered, or every place passed over. */
export type ApogeeBinary =
  | {
      kind: 'found'
      /** The absolute path run. */
      path: string
      source: ApogeeSource
      /** `0.2.0`, the second word of the version line. */
      version: string
      /** The whole first line `version` printed. */
      versionLine: string
      /** The channel the binary says it is (`channel: dev`), when it says one. */
      channel?: string
      skipped: ApogeeSkipped[]
    }
  | { kind: 'not-found'; skipped: ApogeeSkipped[] }

/** The binary as the session knows it. */
export type ApogeeBinaryState = {
  /** What discovery found when it last looked; absent before it has. */
  binary?: ApogeeBinary
  /** True once the not-installed condition was said this session. */
  isAbsenceSaid: boolean
}

/**
 * One backend `apogee_complete` may name, and the most tokens a call of it
 * generates: its own bound (1024, or the entry's own `max_tokens` when lower).
 * `isBoundUnsaid` marks a served backend whose own bound the server's listing
 * did not say: `maxTokens` is then the widest the server takes, and the server
 * holds the backend to its own.
 */
export type ApogeeCompleteBackend = { name: string; maxTokens: number; isBoundUnsaid?: true }

/**
 * How `apogee_complete` reaches the model: the server's own `complete`, where
 * the person's config lists it under a backend's `mcp_serve:` (54b), else the
 * one-shot `apogee complete -m <backend>`.
 */
export type ApogeeCompletePlumbing = 'served' | 'cli'

/** The tools the session was given, as the session start offered them. */
export type ApogeeToolsState = {
  /** The served tools registered, by the server's own names (`models_list`, ...). */
  served: string[]
  /** Why the server's tools are not registered, when they are not. */
  servedRefusal?: string
  /** `apogee_complete` as offered; absent when it is not. */
  complete?: { plumbing: ApogeeCompletePlumbing; backends: ApogeeCompleteBackend[] }
  /** Why `apogee_complete` is not offered, when it is not. */
  completeRefusal?: string
}

/** What a job is: a completion, or one of the two confirmed commands. */
export type ApogeeJobKind = 'complete' | 'promote' | 'rollback'

/** A long run the mod launched and did not wait on (the async law). */
export type ApogeeJob = {
  /** `complete-1`, `promote-2`: the kind and a count. */
  id: string
  kind: ApogeeJobKind
  /** What runs, as the person reads it. */
  title: string
  /** Running; ended, well or not; or lost, its copy of the mod replaced while it ran. */
  state: 'running' | 'done' | 'failed' | 'lost'
  /** Milliseconds since the epoch, by `$.clock`. */
  startedAt: number
  endedAt?: number
  /** The answer (a completion's) or what the command printed. */
  text?: string
  /** Why it failed or was lost. */
  reason?: string
}

/** The jobs of the session, newest last, and the count the next id takes. */
export type ApogeeJobsState = { jobs: ApogeeJob[]; count: number }

/** The status line's reading: the active backend, its promoted version and the run state. */
export type ApogeeStatusReading = {
  /** The backend the chat role resolves to, or none configured; absent when `models status` did not say. */
  backend?: { kind: 'set'; name: string } | { kind: 'unset' }
  /** The active promoted version of that backend; absent when it has none. */
  version?: number
  /** The run state, said (`idle`, `training: 1 run, pipeline nightly`); absent when `train status` did not say. */
  run?: string
  /** What the documents lacked, each `<document> <field>`. */
  missing: string[]
}

/** The status line as last read. */
export type ApogeeStatusState = {
  /** The last reading that succeeded; absent before one has. */
  reading?: ApogeeStatusReading
  /** When it was read (ms, by `$.clock`). */
  readAt?: number
  /** Why the latest attempt failed, when it did. */
  failure?: string
  /** When the latest attempt was made. */
  attemptAt?: number
}

declare module 'claude-code' {
  interface PluginState {
    apogee: {
      binary: Shaped<ApogeeBinaryState>
      tools: Shaped<ApogeeToolsState>
      jobs: Shaped<ApogeeJobsState>
      status: Shaped<ApogeeStatusState>
    }
  }
}
