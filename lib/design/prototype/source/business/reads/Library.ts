import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/**
 * One agent, as `agents list --output-format json` lists it: the Mac app's
 * `AgentEntry` (`Library.swift`), its prompt and schema files (relative to
 * the data directory), its model and tool policy (design 1n).
 */
export interface AgentEntry {
  readonly name: string
  readonly description: string
  /** Shipped with Apogee rather than the person's own. */
  readonly bundled: boolean
  /** The backend override; empty for `models.default`. */
  readonly model: string
  /** `read-only`, `all` or `none`. */
  readonly tools: string
  readonly prompts: readonly string[]
  readonly schemas: readonly string[]
  readonly collection: string
}

const strings = (value: JSONValue | undefined): string[] => (JSONValue.array(value) ?? []).flatMap((item) => (typeof item === 'string' ? [item] : []))

export const AgentEntry = {
  read(value: JSONValue): AgentEntry | undefined {
    const row = JSONValue.object(value)
    const name = JSONValue.string(row?.name)
    if (!row || name === undefined) return undefined
    return {
      name,
      description: JSONValue.string(row.description) ?? '',
      bundled: JSONValue.bool(row.bundled) ?? false,
      model: JSONValue.string(row.model) ?? '',
      tools: JSONValue.string(row.tools) ?? '',
      prompts: strings(row.prompts),
      schemas: strings(row.schemas),
      collection: JSONValue.string(row.collection) ?? '',
    }
  },

  list(document: ReadDocument): AgentEntry[] {
    return (document.entries ?? []).flatMap((entry) => AgentEntry.read(entry) ?? [])
  },
}

/**
 * A configured MCP server, as `mcp list --output-format json` connects to it:
 * the Mac app's `MCPServer` (`Library.swift`), its state and the tools it
 * offers. Its environment is said only as set or not -- never its values
 * (design 1n).
 */
export interface MCPServer {
  readonly name: string
  readonly command: string
  readonly args: readonly string[]
  readonly enabled: boolean
  readonly envSet: boolean
  /** `connected`, or how connecting failed, in the CLI's words. */
  readonly state: string
  readonly tools: readonly string[]
  readonly error: string | undefined
}

export const MCPServer = {
  read(value: JSONValue): MCPServer | undefined {
    const row = JSONValue.object(value)
    const name = JSONValue.string(row?.name)
    if (!row || name === undefined) return undefined
    return {
      name,
      command: JSONValue.string(row.command) ?? '',
      args: strings(row.args),
      enabled: JSONValue.bool(row.enabled) ?? true,
      envSet: JSONValue.bool(row.env_set) ?? false,
      state: JSONValue.string(row.state) ?? '',
      tools: strings(row.tools),
      error: JSONValue.string(row.error),
    }
  },

  list(document: ReadDocument): MCPServer[] {
    return (document.entries ?? []).flatMap((entry) => MCPServer.read(entry) ?? [])
  },
}

/**
 * One row of `mcp presets`: the Mac app's `MCPPreset` (`BusinessReads`,
 * `Library.swift`), the fields Settings › Xcode reads (design 1i); the
 * Library place (1n) reads the same rows.
 */
export interface MCPPreset {
  readonly name: string
  readonly description: string
  readonly provides: string
  /** The command that registers it, as the CLI writes it. */
  readonly register: string | undefined
  /** Why it is not registered into Apogee, in the CLI's words. */
  readonly refusal: string | undefined
  /** Whether this machine can run it, as the binary resolved it here; `undefined` where the row doesn't say. */
  readonly runnable: boolean | undefined
  readonly missing: string | undefined
  readonly remedy: string | undefined
  /** What it needs done outside Apogee, in the words the listing prints under it. */
  readonly note: string | undefined
}

export const MCPPreset = {
  list(document: ReadDocument): MCPPreset[] {
    return (document.entries ?? []).flatMap((entry): MCPPreset[] => {
      const row = JSONValue.object(entry)
      const name = JSONValue.string(row?.name)
      if (!row || name === undefined) return []
      const optional = (key: string) => JSONValue.string(row[key])
      return [
        {
          name,
          description: optional('description') ?? '',
          provides: optional('provides') ?? '',
          register: optional('register'),
          refusal: optional('refusal'),
          runnable: JSONValue.bool(row.runnable),
          missing: optional('missing'),
          remedy: optional('remedy'),
          note: optional('note'),
        },
      ]
    })
  },

  /** The server name its own `register` line gives -- `apogee mcp create <server> --from <preset>` -- never one made up. */
  serverName(preset: MCPPreset): string | undefined {
    const words = (preset.register ?? '').split(' ').filter((word) => word !== '')
    if (words.length !== 6 || words[1] !== 'mcp' || words[2] !== 'create' || words[4] !== '--from' || words[5] !== preset.name) return undefined
    return words[3]
  },

  /** What registering it runs: its line's `mcp create <server> --from <preset>`. */
  registration(preset: MCPPreset): string[] | undefined {
    const server = MCPPreset.serverName(preset)
    return server === undefined ? undefined : ['mcp', 'create', server, '--from', preset.name]
  },
}
