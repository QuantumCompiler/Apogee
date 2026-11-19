import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import type { Timers } from '../../../infrastructure/clock/Clock'
import type { Preset } from '../../../infrastructure/process/Preset'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ModeRules } from '../../../infrastructure/process/StandInChild'
import { ModelsViewModel } from '../../models/ViewModels/ModelsViewModel'
import { MonitorViewModel } from '../../monitor/ViewModels/MonitorViewModel'
import type { Destination } from '../../shell/ViewModels/Destination'
import { ShellViewModel } from '../../shell/ViewModels/ShellViewModel'
import type { SystemStrip } from '../../shell/ViewModels/SystemStrip'
import { ThemeService } from '../../styles/Appearance/ThemeService'

/** A place's view-model, as the composition calls it when the place comes into view. */
interface Place {
  appeared?(): void
}

/**
 * The application's view-model: the composition root (design 1e, ADR 0001
 * §3), the Mac app's `AppViewModel`. It builds the stand-in's services once
 * from the preset -- the child, the clock, the reads -- builds each
 * view-model over them, wires the links between places with closures, and
 * calls `appeared()` on the place each route change shows. The entry file
 * only mounts it. Plain TypeScript: it never imports React.
 */
export class AppViewModel {
  readonly name = 'Apogee'
  readonly clock: Clock
  readonly child: StandInChild
  readonly reads: ReadRunner
  readonly theme: ThemeService
  readonly shell: ShellViewModel
  readonly monitor: MonitorViewModel
  readonly models: ModelsViewModel

  /** Each place's view-model, by its destination: the ones built so far. */
  private readonly places: Partial<Record<Destination, Place>>
  private started = false

  constructor(
    readonly preset: Preset,
    modes: ModeRules,
    timers?: Timers,
  ) {
    this.child = new StandInChild(preset, modes)
    this.clock = new Clock(preset.clock, timers)
    this.reads = new ReadRunner(this.child)
    this.theme = new ThemeService()
    this.shell = new ShellViewModel(this.clock, this.theme)
    this.monitor = new MonitorViewModel(this.reads, this.clock)
    this.models = new ModelsViewModel()
    this.places = { models: this.models }

    // The links between places, as closures: the monitor's two doors.
    this.monitor.openLibrary = () => {
      this.shell.closeOverlay()
      this.shell.go('models')
      this.models.showStore()
    }
    // Until Sessions (1f, 1g), Video (1k), Compose (1l) and Voice (1m) hold
    // what a row unloads or stops, Unload says what the old prototype says.
    this.monitor.unload = (name) => this.shell.say(`Unloaded ${name}`)
    this.shell.routeChanged = (destination) => this.places[destination]?.appeared?.()
    // The monitor reads while it, or the strip that shows its numbers, is open.
    this.shell.subscribe(() => this.watch())
  }

  /** The sidebar strip's numbers: the monitor's. */
  get strip(): SystemStrip | undefined {
    return this.monitor.strip
  }

  /**
   * The app started: every place handed the binary the stand-in finds, the
   * preset's screen set up, then the clock started -- its start ticks applied
   * at once. Once only.
   */
  start(): void {
    if (!this.started) {
      this.started = true
      const found = this.child.locateBinary()
      this.monitor.connect(found.case === 'found' ? found.binary : undefined)
      this.applyStart(this.preset.route)
      this.watch()
    }
    this.clock.start()
  }

  /** The clock stopped: nothing ticks until `start` again. */
  stop(): void {
    this.clock.stop()
  }

  /** What a preset's `start` opens, as the old `applyStart()` sets it up. */
  private applyStart(route: string): void {
    const [where] = route.split(':')
    if (where === 'monitor') {
      this.shell.go('sessions')
      this.shell.open('monitor')
      this.shell.setSystemOpen(true)
    }
  }

  private watch(): void {
    if (!this.started) return
    if (this.shell.overlay === 'monitor' || this.shell.systemOpen) this.monitor.opened()
    else this.monitor.closed()
  }
}
