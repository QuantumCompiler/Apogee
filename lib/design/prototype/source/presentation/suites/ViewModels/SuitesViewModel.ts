import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { Field } from '../../../business/reads/Models'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { Suite, SuiteFootprint, Suites } from '../../../business/reads/SuiteFootprint'
import type { ReadDocument } from '../../../data/protocol/ReadDocument'
import { Observable } from '../../styles/Observation/Observable'
import type { SuiteDetailFace, SuitesFace } from './SuitesFaces'

/** Where the list stands: the Mac app's `reading` and `failure`, and the old place's missing CLI, which draws nothing. */
type Listing = { readonly case: 'notRead' } | { readonly case: 'read' } | { readonly case: 'missing' } | { readonly case: 'failed'; readonly message: string }

/** A suite's fit, as the CLI's read priced it, or why there is none. */
type Fit = { readonly case: 'priced'; readonly footprint: SuiteFootprint } | { readonly case: 'unread'; readonly message: string }

const noDetail: SuiteDetailFace = { shown: false, name: '', desc: '', defaultLabel: '', total: '', tone: 'plain', members: [], unread: [], footer: '', openLabel: '', openHelp: '', switchLabel: '', switchHelp: '', canSwitch: false }

/**
 * The Suites place (design 1p): the Mac app's `SuitesViewModel`
 * (`SuitesViewModel.swift`) -- each suite and its members, read through
 * `config get`, with whether it fits this machine -- drawn as the old
 * prototype's place (`main.html` at `b375fac9`, `ui.isSuites` and the
 * `suites` section of `renderVals()`). The fit is the CLI's verdict alone,
 * `models status --suite <name>`'s footprint, the one `chat --suite` admits a
 * session by: where the Mac estimates a suite no read priced, this place
 * says the read's failure, never a fit worked out here.
 *
 * Where it differs from the Mac's, by the document: New suite… keeps the old
 * prototype's toast (no CLI verb is behind it yet; prototype-only), and the
 * first suite chosen is the default, as the old one opens on its default.
 * Opening a chat under a suite and switching the open session to it are the
 * Sessions place's, reached through closures the composition fills -- this
 * module never imports `session`, `sessions` or `models`. Plain TypeScript.
 */
export class SuitesViewModel extends Observable {
  suites: readonly Suite[] = []
  listing: Listing = { case: 'notRead' }
  selected: string | undefined = undefined
  /** The suite every command resolves under, as `models status` names it. */
  private readDefault: string | undefined = undefined
  /**
   * What `config set-default-suite` reported, kept beside the reads (its
   * `models.default_suite = <name>` line; `''` for none): the stand-in keeps
   * no state, so a read back still names the old default, and the CLI's own
   * report of the change stands over it for the life of the place.
   */
  private reportedDefault: string | undefined = undefined
  private fits = new Map<string, Fit>()
  private descriptions = new Map<string, string>()

  /** The toast, the composition's. */
  say?: (text: string) => void
  /** Open a chat under the suite -- `chat --suite <name>`, `--force` where said -- in the Sessions place: the composition's door. */
  openChat?: (suite: string, force: boolean) => void
  /** Move the open session to the suite, as its own `/suite` does: the composition's door. */
  switchSession?: (suite: string, force: boolean) => void
  /** Whether a session is open that Switch the session to it can move: the session's word, through the composition. */
  sessionSwitchable?: () => boolean
  /** The default changed: the composition tells the places that read it (Models' Roles). */
  defaultChanged?: () => void

  private binary: string | undefined = undefined

  constructor(private readonly runner: CommandRunning) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    if (binary === undefined) this.listing = { case: 'missing' }
    this.changed()
  }

  /** The place came into view: read again. */
  async appeared(): Promise<void> {
    await this.refresh()
  }

  /**
   * The suites (`config get suites`, each `config get suites.<name>`), the
   * default (`models status`'s `suite`), and each suite's footprint as the
   * CLI prices it (`models status --suite <name>`).
   */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const listed = await this.runner.command(binary, ['config', 'get', 'suites'])
    if (listed.case !== 'done') {
      this.listing = { case: 'failed', message: CommandOutcome.failure(listed) ?? 'apogee could not list its suites' }
      this.changed()
      return
    }
    const names = Suites.names(listed.stdout)
    const read: Suite[] = []
    for (const name of names) {
      const members = await this.runner.command(binary, ['config', 'get', `suites.${name}`])
      if (members.case === 'done') read.push(Suite.read(name, members.stdout))
    }
    const status = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'status']))
    const fits = new Map<string, Fit>()
    const descriptions = new Map<string, string>()
    for (const suite of read) {
      const outcome = await this.runner.read(binary, ['models', 'status', '--suite', suite.name])
      const document = ReadOutcome.documentOf(outcome)
      const footprint = document ? SuiteFootprint.of(document) : undefined
      fits.set(suite.name, footprint ? { case: 'priced', footprint } : { case: 'unread', message: ReadOutcome.failure(outcome) ?? `models status --suite ${suite.name} states no footprint` })
      if (document) descriptions.set(suite.name, SuitesViewModel.description(document, suite.name))
    }
    this.suites = read
    this.fits = fits
    this.descriptions = descriptions
    this.readDefault = status ? SuitesViewModel.defaultSuite(status, names) : undefined
    this.listing = { case: 'read' }
    if (this.selected === undefined || !read.some((suite) => suite.name === this.selected)) this.selected = this.defaultSuite ?? read[0]?.name
    this.changed()
  }

  /**
   * The suite every command resolves under, as `models status` names it: its
   * `suite` field matched against the names `config get suites` gave -- the
   * name alone, or the name before the `   (` the CLI writes a described
   * suite's description after (the Mac's `defaultSuite(in:among:)`).
   */
  static defaultSuite(status: ReadDocument, names: readonly string[]): string | undefined {
    const said = Field.list(status).find((field) => field.label === 'suite')?.value
    if (said === undefined) return undefined
    return names.find((name) => said === name || said.startsWith(`${name}   (`))
  }

  /** The description the CLI writes after a described suite's name (`duo   (two)`); empty when it writes none. */
  static description(status: ReadDocument, name: string): string {
    const said = Field.list(status).find((field) => field.label === 'suite')?.value ?? ''
    const head = `${name}   (`
    return said.startsWith(head) && said.endsWith(')') ? said.slice(head.length, -1) : ''
  }

  /** The default the place shows: what the CLI last reported setting, else what it read. */
  get defaultSuite(): string | undefined {
    if (this.reportedDefault !== undefined) return this.reportedDefault === '' ? undefined : this.reportedDefault
    return this.readDefault
  }

  get current(): Suite | undefined {
    return this.suites.find((suite) => suite.name === this.selected)
  }

  select(name: string): void {
    this.selected = name
    this.changed()
  }

  /** Whether the suite's verdict says it does not fit: `chat --suite` refuses it unless forced. */
  private over(name: string): boolean {
    const fit = this.fits.get(name)
    return fit?.case === 'priced' && fit.footprint.verdict === 'doesNotFit'
  }

  /** Whether Switch the session to it can move a session: one open that is not lost if ended. */
  get canSwitchSession(): boolean {
    return this.sessionSwitchable?.() ?? false
  }

  /**
   * Open a chat under it (the Mac's `openChat(under:)`): forced past the
   * CLI's memory refusal only where its verdict says it does not fit, the
   * button saying so.
   */
  openChatUnderIt(): void {
    const suite = this.current
    if (suite) this.openChat?.(suite.name, this.over(suite.name))
  }

  /** Switch the session to it (the Mac's `switchSession(to:)`): the open session moved under the suite, as `/suite` moves it. */
  switchSessionToIt(): void {
    const suite = this.current
    if (!suite || !this.canSwitchSession) return
    this.switchSession?.(suite.name, this.over(suite.name))
  }

  /**
   * Use for new sessions, or Stop using it by default: `config set-default-suite
   * <name>` or `off`, its report kept, the place read back, and the old
   * words said.
   */
  async makeDefault(): Promise<void> {
    const binary = this.binary
    const suite = this.current
    if (binary === undefined || !suite) return
    const on = this.defaultSuite !== suite.name
    const outcome = await this.runner.command(binary, ['config', 'set-default-suite', on ? suite.name : 'off'])
    if (outcome.case !== 'done') {
      this.say?.(`That didn't work: ${CommandOutcome.failure(outcome) ?? 'the default suite could not be set'}`)
      return
    }
    const reported = /^models\.default_suite = (.*)$/m.exec(outcome.stdout)?.[1]?.trim()
    this.reportedDefault = reported === undefined || reported === '(none)' ? '' : reported
    await this.refresh()
    this.say?.(on ? `${suite.name} is the default suite` : 'No default suite')
    this.defaultChanged?.()
  }

  /** New suite…: the old toast. The CLI has no verb a sheet could run yet (prototype-only). */
  newSuite(): void {
    this.say?.('New suite: pick a main model and helpers (prototype)')
  }

  face(): SuitesFace {
    const list = this.suites.map((suite) => {
      const fit = this.fits.get(suite.name)
      return {
        name: suite.name,
        isDefault: this.defaultSuite === suite.name,
        summary: fit?.case === 'priced' ? fit.footprint.verdictWords : 'unread',
        over: this.over(suite.name),
        current: this.selected === suite.name,
      }
    })
    return { list, empty: this.listing.case === 'failed' ? this.listing.message : '', detail: this.detail() }
  }

  private detail(): SuiteDetailFace {
    const suite = this.current
    if (!suite) return noDetail
    const name = suite.name
    const fit = this.fits.get(name)
    const over = this.over(name)
    const footprint = fit?.case === 'priced' ? fit.footprint : undefined
    const forced = over ? ' --force' : ''
    return {
      shown: true,
      name,
      desc: this.descriptions.get(name) ?? '',
      defaultLabel: this.defaultSuite === name ? 'Stop using it by default' : 'Use for new sessions',
      total: footprint ? footprint.total : fit?.case === 'unread' ? fit.message : '',
      tone: footprint?.verdict === 'fits' ? 'fits' : over ? 'over' : 'plain',
      members: (footprint?.members ?? []).map((member) => ({ role: member.roles.join(', '), backend: member.backend, window: Suite.windowOf(suite, member.backend), holds: member.said })),
      unread: suite.unread,
      footer: `apogee's own verdict, from models status --suite ${name}: the one chat --suite admits a session by, refusing a suite that does not fit unless forced.`,
      openLabel: over ? 'Open a chat under it anyway' : 'Open a chat under it',
      openHelp: `apogee chat --suite ${name}${over ? " --force: run it over this machine's memory" : ''}`,
      switchLabel: over ? 'Switch the session to it anyway' : 'Switch the session to it',
      switchHelp: `Carries the open conversation on under ${name}, as /suite does: the session restarts with --resume and --suite${forced}`,
      canSwitch: this.canSwitchSession,
    }
  }
}
