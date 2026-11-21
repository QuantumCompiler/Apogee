import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { MCPPreset, MCPServer } from '../../../business/reads/Library'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { Observable } from '../../styles/Observation/Observable'
import type { LibraryDoors } from './DefinitionEditor'
import { LibraryListing } from './LibraryListing'

/**
 * The Library's MCP servers (design 1n): the Mac app's `MCPViewModel`
 * (`LibraryViewModels.swift:609-739`) -- each server as `mcp list` connects
 * to it, the presets Apogee ships, and a server's removal asked first (GUI
 * 4k), its config entry removed by `config delete-mcp-server` and read back.
 * The first row is the page shown and a row does not pick, as the old
 * prototype draws it; Disable stays its toast (the document's defaults).
 * Plain TypeScript.
 */
export class MCPViewModel extends Observable {
  servers: readonly MCPServer[] = []
  presets: readonly MCPPreset[] = []
  /** Where `mcp list` and `mcp presets` stand, each said apart (GUI 4h). */
  serversListing: LibraryListing = LibraryListing.notRead
  presetsListing: LibraryListing = LibraryListing.notRead
  /** The server whose removal waits on the person's word (GUI 4k). */
  pendingRemove: MCPServer | undefined = undefined
  /** The servers the CLI said it removed: the stand-in keeps no state, so its read lists them still, and the removal it reported is kept beside the read. */
  private removed: readonly string[] = []
  private binary: string | undefined = undefined

  constructor(
    private readonly runner: CommandRunning,
    private readonly doors: LibraryDoors,
  ) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    if (binary === undefined) {
      this.serversListing = LibraryListing.missing
      this.presetsListing = LibraryListing.missing
    }
    this.changed()
  }

  /** `mcp list` and `mcp presets`, each failure said where its list is, the rows read before kept. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const list = await this.runner.read(binary, ['mcp', 'list'])
    const listed = ReadOutcome.documentOf(list)
    if (listed) this.servers = MCPServer.list(listed)
    this.serversListing = LibraryListing.of(list)
    const shipped = await this.runner.read(binary, ['mcp', 'presets'])
    const presets = ReadOutcome.documentOf(shipped)
    if (presets) this.presets = MCPPreset.list(presets)
    this.presetsListing = LibraryListing.of(shipped)
    this.changed()
  }

  /** The servers listed: the read, without the ones the CLI said it removed. */
  get listed(): readonly MCPServer[] {
    return this.servers.filter((server) => !this.removed.includes(server.name))
  }

  /** What the servers' list says under its rows. */
  get serversText(): string | undefined {
    return LibraryListing.text(this.serversListing, 'MCP servers')
  }

  /** What the presets say in their place. */
  get presetsText(): string | undefined {
    return LibraryListing.text(this.presetsListing, 'presets')
  }

  /** The page shown: the first server listed, as the old page shows its first row. */
  get current(): MCPServer | undefined {
    return this.listed[0]
  }

  /** Disable (or Enable): the old prototype's toast, word for word. */
  toggle(): void {
    const server = this.current
    if (!server) return
    this.doors.say?.(`apogee mcp ${server.enabled ? 'disable' : 'enable'} ${server.name} (prototype)`)
  }

  /** Remove…: asked first (GUI 4k). */
  askToRemove(): void {
    const server = this.current
    if (!server) return
    this.pendingRemove = server
    this.changed()
    this.doors.ask?.('mcpRemove')
  }

  /** The question's words. */
  get removeWords(): { readonly title: string; readonly body: string } | undefined {
    const name = this.pendingRemove?.name
    if (name === undefined) return undefined
    return { title: `Remove the MCP server ${name}?`, body: `apogee config delete-mcp-server ${name} -- its entry leaves the config, so no session connects to it; its files are left alone.` }
  }

  dismissRemove(): void {
    this.pendingRemove = undefined
    this.changed()
  }

  /** `config delete-mcp-server <name>` once the person said yes -- its config entry removed, its files left alone, as the CLI says -- read back by `mcp list`. */
  async confirmRemove(): Promise<void> {
    const server = this.pendingRemove
    const binary = this.binary
    this.pendingRemove = undefined
    if (!server || binary === undefined) return
    const outcome = await this.runner.command(binary, ['config', 'delete-mcp-server', server.name])
    await this.refresh()
    const failure = CommandOutcome.failure(outcome)
    if (failure !== undefined) {
      this.doors.say?.(failure)
    } else if (outcome.case === 'done') {
      this.removed = [...this.removed, server.name]
      this.doors.say?.(outcome.stdout.split('\n')[0] || 'Done')
    }
    this.changed()
  }
}
