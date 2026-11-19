import type { Clock } from '../../../infrastructure/clock/Clock'
import { layout } from '../../styles/Tokens/Tokens'
import type { ThemeService } from '../../styles/Appearance/ThemeService'
import { themeName } from '../../styles/Appearance/Appearance'
import { Observable } from '../../styles/Observation/Observable'
import type { Destination, Overlay } from './Destination'
import { commandFor } from './KeyMap'
import type { KeyChord, ShellCommand } from './KeyMap'

/** One line at the bottom of the window, and the tick it was said on. */
export interface Toast {
  readonly id: number
  readonly text: string
  readonly at: number
}

/** How the window lays out at its width. */
export interface ShellLayout {
  /** At or below tokens.json's `collapseBelow` (900): the sidebar a column of icons, the side panes closed. */
  readonly collapsed: boolean
  /** At or below `narrowBelow` (600): a message row narrows. */
  readonly narrow: boolean
}

/** What the shell needs of the clock: its ticks, to clear the toast. */
export type Ticks = Pick<Clock, 'ticks' | 'onTick'>

/**
 * The window around every screen (design 1e): which place it shows, which
 * overlay is open, the sidebar, the open menu, the sheet and the confirm, the
 * toast and the theme button. The Mac app's `ShellViewModel`, on the
 * prototype's clock: the toast clears after 12 ticks, as the old `tick()`
 * clears it. Plain TypeScript: it never imports React or a place.
 */
export class ShellViewModel extends Observable {
  /** How long a toast shows, in ticks of the clock: the old `clock - toastAt > 12`. */
  static readonly toastTicks = 12

  route: Destination = 'sessions'
  overlay: Overlay | undefined = undefined
  /** Settings, open over the window (1i draws its panes). */
  settingsOpen = false
  /** The sidebar's SYSTEM strip, open. */
  systemOpen = false
  /** Hidden by the person (⌃⌘S). */
  sidebarHidden = false
  /** The open menu, by its owner's key (`row:<id>`, `model`): what the scrim closes. */
  menu: string | undefined = undefined
  /** The sheet open over the window, by kind; its place supplies what it says. */
  sheet: string | undefined = undefined
  /** The confirm asking over the window, by kind; its place supplies what it says. */
  confirm: string | undefined = undefined
  toast: Toast | undefined = undefined
  /** The window's width, as last laid out; `undefined` before the first layout. */
  width: number | undefined = undefined
  /** Told each time the place shown changes: how a place learns it came into view. */
  routeChanged: ((destination: Destination) => void) | undefined = undefined

  private nextToast = 0

  constructor(
    private readonly clock: Ticks,
    readonly theme: ThemeService,
  ) {
    super()
    clock.onTick((tick) => {
      if (this.toast && tick - this.toast.at > ShellViewModel.toastTicks) {
        this.toast = undefined
        this.changed()
      }
    })
  }

  /** How the window lays out at its width: tokens.json's thresholds, at or below each, as the old `max-width` is. */
  get layout(): ShellLayout {
    const width = this.width ?? Number.POSITIVE_INFINITY
    return { collapsed: width <= layout.collapseBelow, narrow: width <= layout.narrowBelow }
  }

  /** Whether a menu is open, so the scrim covers the window. */
  get menuOpen(): boolean {
    return this.menu !== undefined
  }

  /** The theme button's words: the theme chosen, or Automatic. */
  get themeName(): string {
    return themeName(this.theme.appearance.themeChoice)
  }

  /** Runs the shell command a key names; whether the shell owns that key. */
  press(keys: KeyChord): boolean {
    const command = commandFor(keys)
    if (command === undefined) return false
    this.perform(command)
    return true
  }

  perform(command: ShellCommand): void {
    switch (command.kind) {
      case 'go':
        this.go(command.destination)
        return
      case 'modelsLibrary':
        this.go('models')
        return
      case 'toggleMonitor':
        this.update(() => (this.overlay = this.overlay === 'monitor' ? undefined : 'monitor'))
        return
      case 'toggleConsole':
        this.update(() => (this.overlay = this.overlay === 'console' ? undefined : 'console'))
        return
      case 'toggleSidebar':
        this.update(() => (this.sidebarHidden = !this.sidebarHidden))
        return
      case 'openSettings':
        this.update(() => (this.settingsOpen = true))
        return
    }
  }

  /** Shows `destination`, and tells whoever listens. */
  go(destination: Destination): void {
    if (destination === this.route) return
    this.update(() => {
      this.route = destination
      this.menu = undefined
    })
    this.routeChanged?.(destination)
  }

  open(overlay: Overlay): void {
    this.update(() => (this.overlay = overlay))
  }

  closeOverlay(): void {
    this.update(() => (this.overlay = undefined))
  }

  closeSettings(): void {
    this.update(() => (this.settingsOpen = false))
  }

  toggleSystem(): void {
    this.setSystemOpen(!this.systemOpen)
  }

  setSystemOpen(open: boolean): void {
    this.update(() => (this.systemOpen = open))
  }

  openMenu(owner: string): void {
    this.update(() => (this.menu = owner))
  }

  /** What the scrim does: every menu closed. */
  closeMenus(): void {
    this.update(() => (this.menu = undefined))
  }

  openSheet(kind: string): void {
    this.update(() => (this.sheet = kind))
  }

  closeSheet(): void {
    this.update(() => (this.sheet = undefined))
  }

  ask(kind: string): void {
    this.update(() => (this.confirm = kind))
  }

  dismissConfirm(): void {
    this.update(() => (this.confirm = undefined))
  }

  /** Says `text` in the toast, from this tick. Never the only place something is said. */
  say(text: string): void {
    this.nextToast += 1
    this.update(() => (this.toast = { id: this.nextToast, text, at: this.clock.ticks }))
  }

  /** The next theme, said in the toast as the old `cycleTheme` says it. */
  cycleTheme(): void {
    this.say(`Theme: ${this.theme.cycleTheme()}`)
  }

  /** The window was laid out `width` px wide. */
  windowResized(width: number): void {
    if (this.width === width) return
    this.update(() => (this.width = width))
  }

  private update(change: () => void): void {
    change()
    this.changed()
  }
}
