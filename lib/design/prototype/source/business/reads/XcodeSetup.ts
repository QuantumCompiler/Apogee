import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'
import { CommandOutcome } from './CommandRunning'

/** One environment variable the agent is started with. */
export interface AgentVariable {
  readonly name: string
  readonly value: string
}

/**
 * The fields Xcode's Add an Agent sheet takes, as `acp registration
 * --output-format json` prints them (the CLI's 56d): the Mac app's
 * `AgentRegistration` (`XcodeSetup.swift`). Settings › Xcode draws each as
 * printed, and composes none.
 */
export interface AgentRegistration {
  readonly location: string
  readonly name: string
  readonly executable: string
  /** `undefined` -- the document's `null` -- for a binary: the sheet's Interpreter left empty. */
  readonly interpreter: string | undefined
  readonly arguments: readonly string[]
  /** In the order the document prints them, by name. */
  readonly environment: readonly AgentVariable[]
  readonly channel: string | undefined
  readonly xcodeMinimum: string | undefined
  readonly approval: string | undefined
}

export const AgentRegistration = {
  object: 'acp.registration',

  /** The document, when it is a registration with an Executable; `undefined` for any other. */
  of(document: ReadDocument): AgentRegistration | undefined {
    const value = document.value
    const executable = JSONValue.string(value.executable)
    if (document.object !== AgentRegistration.object || executable === undefined || executable === '') return undefined
    const environment = Object.entries(JSONValue.object(value.environment) ?? {})
      .flatMap(([name, entry]) => (typeof entry === 'string' ? [{ name, value: entry }] : []))
      .sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0))
    return {
      location: JSONValue.string(value.where) ?? '',
      name: JSONValue.string(value.name) ?? '',
      executable,
      interpreter: JSONValue.string(value.interpreter),
      arguments: (JSONValue.array(value.arguments) ?? []).flatMap((word) => (typeof word === 'string' ? [word] : [])),
      environment,
      channel: JSONValue.string(value.channel),
      xcodeMinimum: JSONValue.string(value.xcode_minimum),
      approval: JSONValue.string(value.approval),
    }
  },

  /** The line the CLI prints above the fields (`render_acp_registration`), from the document's own words; `undefined` where it printed no place. */
  heading(fields: AgentRegistration): string | undefined {
    if (fields.location === '') return undefined
    const xcode = fields.xcodeMinimum === undefined ? 'Xcode' : `Xcode ${fields.xcodeMinimum} or later`
    return `Add ${fields.name} to ${xcode}: ${fields.location}`
  },
}

/** `check`'s `Xcode` section (the CLI's 56d), by the names it gives its rows. */
export const XcodeCheck = {
  section: 'Xcode',
  xcode: 'xcode',
  bridge: 'mcpbridge',
  agents: 'acp agents',
  approval: 'approval',
} as const

/**
 * Whether an MCP server is in the config, as `config get mcp_servers.<name>`
 * says it -- its command, or `no such config key` when there is none: a plain
 * config read, which connects to nothing. The Mac's `MCPServerEntry`.
 */
export type MCPServerEntry = { readonly case: 'registered'; readonly command: string } | { readonly case: 'notRegistered' } | { readonly case: 'unreadable'; readonly reason: string }

export const MCPServerEntry = {
  key(server: string): string {
    return `mcp_servers.${server}`
  },

  noSuchKey: 'no such config key',

  of(outcome: CommandOutcome): MCPServerEntry {
    if (outcome.case === 'done') return { case: 'registered', command: outcome.stdout.trim() }
    if (outcome.case === 'failed' && outcome.stderrTail.text.includes(MCPServerEntry.noSuchKey)) return { case: 'notRegistered' }
    return { case: 'unreadable', reason: CommandOutcome.failure(outcome) ?? 'it could not be read' }
  },
}
