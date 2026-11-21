import { MediaLibrary } from './MediaRecords'

/**
 * The install a media verb's refusal named -- `train setup --with video`,
 * `--with voice` -- waiting on the person's yes. The Mac app's
 * `InstallConsent` (`BusinessMedia`, `InstallConsent.swift`; GUI 1m, 1o, 4f).
 *
 * It is read from the run's own words, never guessed: the CLI refuses a
 * clip whose runtime is not installed by naming the command that installs it
 * (`… -- apogee train setup --with video`). Since the CLI's M29 that refusal
 * ends the job's stream on an `error` event, which is read first; a binary
 * before it says it on stderr alone.
 */
export interface InstallConsent {
  /** What the install runs: `train setup --with video`. */
  readonly arguments: readonly string[]
  /** The refusal, in the CLI's words: the line that named the install. */
  readonly said: string
}

/** The line of `text` that names the install, trimmed. */
function naming(text: string): string | undefined {
  return text
    .split(/\r?\n/)
    .filter((line) => line.includes('apogee train setup'))
    .at(-1)
    ?.trim()
}

export const InstallConsent = {
  /**
   * The consent a failed run asks for, read where the run said why: the
   * stream's own `error` when one arrived -- its words alone -- else
   * `failure`, the run's last word, with the `stderr` it left. `undefined`
   * for a failure of any other kind.
   */
  fromRun(error: string | undefined, failure: string, stderr: string): InstallConsent | undefined {
    if (error === undefined) return InstallConsent.fromFailure(failure, stderr)
    const args = MediaLibrary.installCommand(error)
    return args === undefined ? undefined : { arguments: args, said: naming(error) ?? error }
  },

  /** The consent a failed run asks for when its `failure` and the `stderr` it left name an install; the fallback where no `error` event came. */
  fromFailure(failure: string, stderr: string): InstallConsent | undefined {
    const text = `${failure}\n${stderr}`
    const args = MediaLibrary.installCommand(text)
    return args === undefined ? undefined : { arguments: args, said: naming(text) ?? failure }
  },

  /**
   * The consent a failed install asks for next: the command its own failure
   * names, when that is another than the install `ran` (the CLI's M19: a
   * rebuild). `undefined` when the failure names nothing, or the same install.
   */
  afterFailed(ran: readonly string[], failure: string | undefined, stderr: string): InstallConsent | undefined {
    const next = InstallConsent.fromFailure(failure ?? '', stderr)
    return next === undefined || next.arguments.join(' ') === ran.join(' ') ? undefined : next
  },

  /** It replaces the environment rather than adding to it (the CLI's M19). */
  rebuilds(consent: InstallConsent): boolean {
    return MediaLibrary.rebuilds(consent.arguments)
  },

  /**
   * Why this install cannot start now, as the place's toast says it, or
   * `undefined` when it can (GUI 6c): one install into the one environment at
   * a time. `installing` names an install another place runs, which every
   * install waits for; `usingEnvironment` a run on the environment's Python,
   * which only a rebuild waits for. Each is the command as the children
   * registry labels it. The consent stands, so Install works again later.
   */
  waits(consent: InstallConsent, installing: string | undefined, usingEnvironment: string | undefined): string | undefined {
    const button = InstallConsent.rebuilds(consent) ? 'Rebuild' : 'Install'
    if (installing !== undefined) return `${installing} is already running: press ${button} again once it has finished`
    if (InstallConsent.rebuilds(consent) && usingEnvironment !== undefined) return `${usingEnvironment} is using Apogee's Python environment: press Rebuild again once it has finished`
    return undefined
  },

  /** What a failed install says in place of asking for this one next while another place's install runs (GUI 6c). */
  heldBack(consent: InstallConsent, installing: string): string {
    return `${installing} is already running: once it has finished, ${InstallConsent.command(consent)} ${InstallConsent.rebuilds(consent) ? 'rebuilds the environment' : 'installs it'}`
  },

  /** The command as a person would type it: `apogee train setup --with video`. */
  command(consent: InstallConsent): string {
    return ['apogee', ...consent.arguments].join(' ')
  },

  /** The set it installs, as the CLI names it: `video`, `voice`. */
  set(consent: InstallConsent): string {
    return consent.arguments.at(-1) ?? ''
  },
}
