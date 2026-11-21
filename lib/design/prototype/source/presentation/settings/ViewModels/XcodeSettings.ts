import type { CheckRow } from '../../../business/reads/Check'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { MCPPreset } from '../../../business/reads/Library'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { AgentRegistration, MCPServerEntry, XcodeCheck } from '../../../business/reads/XcodeSetup'
import { Observable } from '../../styles/Observation/Observable'

/** What the binary said of one of the pane's reads: not asked yet, what it printed, or why it printed nothing usable. */
export type Read<Value> = { readonly case: 'notRead' } | { readonly case: 'read'; readonly value: Value } | { readonly case: 'failed'; readonly reason: string }

/** What a change said, every line as the CLI said it, under the app's line for what was done: the Mac's `Said`. */
export interface Said {
  readonly title: string
  readonly lines: readonly string[]
}

/** A confirm's words, as the shell's confirm host draws them. */
export interface ConfirmWords {
  readonly title: string
  readonly body: string
  readonly confirm: string
}

/** One line of Xcode's Add an Agent sheet, as `acp registration` printed it: the Mac's `RegistrationField`. */
export interface RegistrationField {
  readonly label: string
  /** What goes in it, exactly as printed; `(leave empty)` for a field left empty. */
  readonly value: string
  readonly canCopy: boolean
  readonly valueColor: string
  readonly aria: string
}

/** Everything the Xcode pane draws, as words. */
export interface XcodeFace {
  /** `check`'s Xcode rows, ok rows included, each its mark and its remedy. */
  readonly rows: ReadonlyArray<{ readonly text: string; readonly mark: string; readonly markColor: string; readonly color: string; readonly remedy: string }>
  /** What the pane says in their place: not read yet, or that this apogee checks nothing of Xcode. */
  readonly rowsLacking: string
  readonly tools: {
    readonly lacking: string
    readonly description: string
    readonly state: string
    readonly stateColor: string
    readonly missing: string
    readonly note: string
    readonly canRegister: boolean
    readonly command: string
    readonly canRemove: boolean
    readonly working: boolean
  }
  readonly failure: string
  readonly said: { readonly shown: boolean; readonly title: string; readonly text: string }
  readonly agent: {
    readonly lacking: string
    readonly heading: string
    readonly fields: readonly RegistrationField[]
    readonly approval: string
    /** `acp registration` printed its fields, but check's rows say this Mac's Xcode can't host an agent. */
    readonly notOffered: boolean
  }
}

/**
 * Settings › Xcode (design 1i): the Mac app's `XcodeSettings` (GUI 5a) --
 * the two routes joining Apogee and Xcode, each in the CLI's words: what
 * `check` says of Xcode, Xcode's tools given to Apogee's agents through the
 * CLI's `xcode` preset, and the fields Xcode's Add an Agent sheet takes.
 * Nothing here reaches Xcode: it reads `mcp presets`, `config get
 * mcp_servers.<server>` and `acp registration`, and takes `check`'s rows from
 * the read Settings already made; Register only writes the config (`mcp
 * create`), and Remove only removes the entry. Drawn as the old prototype's
 * pane. Plain TypeScript: no React.
 */
export class XcodeSettings extends Observable {
  /** `mcp presets`' `xcode` row: `undefined` inside `read` where this binary lists none. */
  preset: Read<MCPPreset | undefined> = { case: 'notRead' }
  /** Whether the preset's server is in the config (`config get`). */
  entry: MCPServerEntry | undefined = undefined
  /** The fields `acp registration` printed, or why it printed none. */
  registration: Read<AgentRegistration> = { case: 'notRead' }
  said: Said | undefined = undefined
  failure: string | undefined = undefined
  working: string | undefined = undefined

  /** The rows Settings read from `check`, `undefined` until it read them: handed in, so the pane runs no `check` of its own. */
  checkRows: () => readonly CheckRow[] | undefined = () => undefined
  /** Asks `check` again after a change, so this pane and Health both follow the config. */
  rereadCheck: () => Promise<void> = async () => {}
  say: ((text: string) => void) | undefined = undefined
  /** Asks the person first, in the shell's confirm host: `xcodeRemove`. */
  ask: ((kind: 'xcodeRemove') => void) | undefined = undefined

  static readonly presetName = 'xcode'
  private binary: string | undefined = undefined

  constructor(private readonly runner: CommandRunning) {
    super()
  }

  /** The binary this launch found; a new one is read afresh. */
  use(binary: string | undefined): void {
    if (binary === this.binary) return
    this.binary = binary
    this.preset = { case: 'notRead' }
    this.entry = undefined
    this.registration = { case: 'notRead' }
    this.said = undefined
    this.failure = undefined
    this.changed()
  }

  /** The pane's own reads, run when it is chosen and after each of its changes: `mcp presets`, `config get` of the server its line names, `acp registration`. */
  async read(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const listed = await this.runner.read(binary, ['mcp', 'presets'])
    this.preset = listed.case === 'document' ? { case: 'read', value: MCPPreset.list(listed.document).find((row) => row.name === XcodeSettings.presetName) } : { case: 'failed', reason: ReadOutcome.failure(listed) ?? 'apogee printed no presets' }
    const server = this.xcodePreset === undefined ? undefined : MCPPreset.serverName(this.xcodePreset)
    this.entry = server === undefined ? undefined : MCPServerEntry.of(await this.runner.command(binary, ['config', 'get', MCPServerEntry.key(server)]))
    const printed = await this.runner.read(binary, ['acp', 'registration'])
    const fields = printed.case === 'document' ? AgentRegistration.of(printed.document) : undefined
    this.registration = fields ? { case: 'read', value: fields } : { case: 'failed', reason: ReadOutcome.failure(printed) ?? 'apogee printed no registration' }
    this.changed()
  }

  /** The `xcode` preset, where this binary lists one. */
  get xcodePreset(): MCPPreset | undefined {
    return this.preset.case === 'read' ? this.preset.value : undefined
  }

  /** `check`'s Xcode section: not read, none (a binary before the CLI's 56d), or every row. */
  get section(): 'notRead' | 'none' | readonly CheckRow[] {
    const rows = this.checkRows()
    if (rows === undefined) return 'notRead'
    const xcode = rows.filter((row) => row.section === XcodeCheck.section)
    return xcode.length === 0 ? 'none' : xcode
  }

  /** Whether `check` read the section and its row of `name` isn't `ok`: the route it speaks for needs an Xcode this Mac lacks. */
  private rulesOut(name: string): boolean {
    const section = this.section
    if (typeof section === 'string') return false
    return section.find((row) => row.name === name)?.status !== 'ok'
  }

  get xcodeAbsent(): boolean {
    return this.rulesOut(XcodeCheck.xcode)
  }

  get registered(): boolean {
    return this.entry?.case === 'registered'
  }

  /** What Register runs -- the preset row's own line -- where the row says it runs, its server is not registered, and `check` does not rule it out. */
  get registers(): string[] | undefined {
    const preset = this.xcodePreset
    if (!preset || preset.runnable !== true || this.entry?.case !== 'notRegistered' || this.xcodeAbsent || this.rulesOut(XcodeCheck.bridge)) return undefined
    return MCPPreset.registration(preset)
  }

  /** What Remove runs, `config delete-mcp-server <server>`, where the server is in the config. */
  get removes(): string[] | undefined {
    const server = this.xcodePreset === undefined ? undefined : MCPPreset.serverName(this.xcodePreset)
    return this.registered && server !== undefined ? ['config', 'delete-mcp-server', server] : undefined
  }

  /** Registers the preset -- its own line, `mcp create <server> --from xcode` -- read back, its words said. */
  async register(): Promise<void> {
    const args = this.registers
    if (args) await this.change(args, "Registered for Apogee's agents")
  }

  /** Remove…: asked first, as the old pane's `xcodeRemove` confirm. */
  askToRemove(): void {
    if (this.removes) this.ask?.('xcodeRemove')
  }

  /** The confirm's words: the old prototype's. */
  get confirmWords(): ConfirmWords | undefined {
    const args = this.removes
    if (!args) return undefined
    return { title: "Remove Xcode's tools from Apogee?", body: `apogee ${args.join(' ')} -- its config entry goes; Xcode is left as it is.`, confirm: 'Remove' }
  }

  /** Remove, once the person said yes: the config entry alone, read back. */
  async remove(): Promise<void> {
    const args = this.removes
    if (args) await this.change(args, "Removed from Apogee's agents")
  }

  dismissSaid(): void {
    this.said = undefined
    this.changed()
  }

  /** A field's Copy: the view writes the clipboard; the toast says so, as the old pane says it. */
  copied(): void {
    this.say?.('Copied to the clipboard')
  }

  private async change(args: readonly string[], title: string): Promise<void> {
    const binary = this.binary
    if (binary === undefined || this.working !== undefined) return
    this.working = args.slice(0, 3).join(' ')
    this.said = undefined
    this.changed()
    const outcome = await this.runner.command(binary, args)
    this.working = undefined
    await this.read()
    await this.rereadCheck()
    if (outcome.case === 'done') {
      this.failure = undefined
      const lines = outcome.stdout.replace(/\n+$/, '').split('\n')
      this.said = { title, lines }
      this.say?.(lines[0] ?? title)
    } else {
      const why = CommandOutcome.failure(outcome) ?? 'apogee said nothing'
      this.failure = why
      this.say?.(`That didn't work: ${why}`)
    }
    this.changed()
  }

  face(): XcodeFace {
    const section = this.section
    const preset = this.xcodePreset
    const entry = this.entry
    const fields = this.registration.case === 'read' && !this.xcodeAbsent && !this.rulesOut(XcodeCheck.agents) ? this.registration.value : undefined
    const registers = this.registers
    const missing = preset && preset.runnable !== true ? [preset.missing, preset.remedy].filter((part) => part !== undefined).join(' -- ') : ''
    const field = (label: string, value: string | undefined): RegistrationField => ({
      label,
      value: value ?? '(leave empty)',
      canCopy: value !== undefined,
      valueColor: value === undefined ? 'var(--text-tertiary)' : 'var(--text-primary)',
      aria: `Copy ${label || 'the next entry'}`,
    })
    return {
      rows:
        typeof section === 'string'
          ? []
          : section.map((row) => ({
              text: `${row.name}: ${row.detail}`,
              mark: row.status === 'ok' ? '✓' : '–',
              markColor: row.status === 'ok' ? 'var(--status-accent)' : 'var(--text-tertiary)',
              color: row.status === 'ok' ? 'var(--text-primary)' : 'var(--text-secondary)',
              remedy: row.remedy ?? '',
            })),
      rowsLacking: section === 'notRead' ? 'Not read yet.' : section === 'none' ? 'This apogee checks nothing of Xcode.' : '',
      tools: {
        lacking: this.preset.case === 'failed' ? `The presets couldn't be read: ${this.preset.reason}` : this.preset.case === 'read' && preset === undefined ? 'This apogee ships no Xcode preset.' : '',
        description: preset?.description ?? '',
        state:
          entry?.case === 'registered'
            ? `In Apogee's config as ${(preset && MCPPreset.serverName(preset)) ?? preset?.name ?? ''}: ${entry.command}`
            : entry?.case === 'notRegistered'
              ? "Not in Apogee's config."
              : entry?.case === 'unreadable'
                ? `Its config entry couldn't be read: ${entry.reason}`
                : '',
        stateColor: entry?.case === 'registered' ? 'var(--text-primary)' : entry?.case === 'unreadable' ? 'var(--status-warning)' : 'var(--text-secondary)',
        missing: missing === '' ? '' : `It can't run here: ${missing}`,
        note: preset?.note ?? '',
        canRegister: registers !== undefined,
        command: registers ? `apogee ${registers.join(' ')}` : '',
        canRemove: this.removes !== undefined,
        working: this.working !== undefined,
      },
      failure: this.failure ?? '',
      said: { shown: this.said !== undefined, title: this.said?.title ?? '', text: this.said?.lines.join('\n') ?? '' },
      agent: {
        lacking: this.registration.case === 'failed' ? `This apogee printed no registration: ${this.registration.reason}` : '',
        heading: fields ? (AgentRegistration.heading(fields) ?? '') : '',
        fields: fields
          ? [
              field('Name', fields.name),
              field('Executable', fields.executable),
              field('Interpreter', fields.interpreter),
              ...fields.arguments.map((argument, index) => field(index === 0 ? 'Arguments' : '', argument)),
              ...fields.environment.map((variable, index) => field(index === 0 ? 'Environment Variables' : '', `${variable.name}=${variable.value}`)),
            ]
          : [],
        approval: fields?.approval ?? '',
        notOffered: fields === undefined && this.registration.case === 'read',
      },
    }
  }
}
