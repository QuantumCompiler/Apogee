import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

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
