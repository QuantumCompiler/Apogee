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

declare module 'claude-code' {
  interface PluginState {
    apogee: { binary: Shaped<ApogeeBinaryState> }
  }
}
