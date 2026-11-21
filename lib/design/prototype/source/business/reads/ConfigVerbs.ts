import { CommandOutcome } from './CommandRunning'

/** Lines with anything on them, trimmed, in order. */
function lines(text: string): string[] {
  return text
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line !== '')
}

/** A tool's gate level, as `config get permissions` prints it: `write_file: ask`. */
export interface PermissionLevel {
  readonly tool: string
  readonly level: string
}

/**
 * The permission gate's levels (design 1i): `config get permissions`, one
 * `tool: level` a line, the tools the config lists (the CLI's
 * `config_cmd.cpp`); a tool it does not list is `ask`, "an answer, not a
 * missing key". The Mac's `SettingsViewModel.levels(in:)`.
 */
export const Permissions = {
  /** The three levels `config set-permission` takes, in its order. */
  choices: ['ask', 'allow', 'deny'] as readonly string[],

  /** Each `tool: level` line, in the CLI's order. */
  levels: (text: string): PermissionLevel[] =>
    lines(text).flatMap((line) => {
      const at = line.indexOf(':')
      if (at <= 0) return []
      return [{ tool: line.slice(0, at).trim(), level: line.slice(at + 1).trim() }]
    }),

  /** `tool`'s level: the one listed, else `ask`. */
  level(levels: readonly PermissionLevel[], tool: string): string {
    return levels.find((entry) => entry.tool === tool)?.level ?? 'ask'
  },

  /**
   * A config value `config get` printed, or `undefined` when the config does
   * not set it: an unset key prints an empty line and exits 0, so an empty
   * answer is unset, as a refusal is (the Mac's `value`, GUI 6c).
   */
  value(outcome: CommandOutcome): string | undefined {
    if (outcome.case !== 'done') return undefined
    const value = outcome.stdout.trim()
    return value === '' ? undefined : value
  },
}

/** The hosts `fetch_url` reaches without asking (the CLI's M33): the Mac's `AllowedHosts`. */
export const AllowedHosts = {
  /** The key, as `config get` takes it. */
  key: 'tools.allowed_hosts',
  /** The hosts, in the CLI's order; none for an empty answer. */
  list: lines,
}

/**
 * A backend's recorded consent for its vendor to run tools of its own under
 * the person's key (the CLI's 47a `server_tools`), as `config get
 * backends.<name>.server_tools` prints it since 55g: one name a line. The
 * Mac's `ServerTools`.
 */
export const ServerTools = {
  /** The backend types whose vendor runs tools of its own: the three APIs; the CLI refuses the key on every other. */
  vendorTypes: ['anthropic', 'openai', 'google'] as readonly string[],
  held(backendType: string): boolean {
    return ServerTools.vendorTypes.includes(backendType)
  },
  key(backend: string): string {
    return `backends.${backend}.server_tools`
  },
  list: lines,
}
