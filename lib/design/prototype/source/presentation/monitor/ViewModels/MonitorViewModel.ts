import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { SystemReading } from '../../../business/reads/SystemReading'
import type { Clock } from '../../../infrastructure/clock/Clock'
import type { SystemStrip } from '../../shell/ViewModels/SystemStrip'
import { Observable } from '../../styles/Observation/Observable'

/** What the monitor needs of the clock: its ticks, to read every 2 s. */
export type Ticks = Pick<Clock, 'ticks' | 'onTick'>

/** A loaded model's row: its name, what holds it, its size when known, and its button. */
export interface MonitorModelRow {
  readonly name: string
  readonly detail: string
  /** Empty: the CLI reports no size for a model held. */
  readonly size: string
  readonly action: string
}

/**
 * Everything the monitor's view draws, as words and numbers (design 1e): the
 * old prototype's regions -- the machine, CPU, GPU, memory, temperature and
 * power, the loaded models, the model store -- each from the `system`
 * reading, or said unknown. The view draws only this, so a test can walk it.
 */
export interface MonitorFace {
  /** `reading` once there is one; `failed` with none and a failure; `waiting` before the first. */
  readonly state: 'reading' | 'failed' | 'waiting'
  /** Why the latest read failed, in the CLI's words or the Mac app's; absent when it did not. */
  readonly failure?: string
  /** `THIS MAC · APPLE M3 MAX · 128 GB`. */
  readonly machine: string
  readonly cpu: { readonly value: string; readonly bars: readonly number[] }
  /** `stale` is absent unless the latest read failed and a reading stands. */
  readonly gpu: { readonly value: string; readonly detail: string; readonly stale?: string }
  readonly memory: {
    readonly value: string
    readonly warning: boolean
    readonly apogee: string
    readonly others: string
    readonly free: string
    /** Each part's weight in the memory bar, in GB. */
    readonly shares: { readonly apogee: number; readonly others: number; readonly free: number }
  }
  readonly models: { readonly total: string; readonly rows: readonly MonitorModelRow[]; readonly empty: boolean }
  readonly store: {
    readonly heading: string
    readonly total: string
    readonly rows: ReadonlyArray<{ readonly label: string; readonly size: string; readonly percent: number }>
    readonly free: string
  }
}

const gib = 2 ** 30

/**
 * The system monitor (design 1e): `apogee system` read through the stand-in
 * every 2 s -- 5 ticks of the clock -- while it is open, and only then; the
 * last 30 readings kept for the history bars; a failed read keeping the last
 * reading, said stale. The Mac app's `MonitorViewModel`, its names
 * (`connect`, `opened`, `closed`, `read`), over the stand-in's clock rather
 * than real time. Plain TypeScript: it never imports React.
 */
export class MonitorViewModel extends Observable {
  /** Ticks between reads: 2 s on the 400 ms clock. */
  static readonly intervalTicks = 5
  /** Readings the history bars keep. */
  static readonly historyLength = 30
  /** The Mac app's words when there is no binary to ask. */
  static readonly notInstalled = 'Apogee isn’t installed, so the system can’t be read.'

  reading: SystemReading | undefined = undefined
  failure: string | undefined = undefined
  /** Whether it is reading now: the monitor, or the strip that shows its numbers, is open. */
  isOpen = false
  /** The CPU's percent in each of the last 30 good readings, oldest first. */
  history: number[] = []
  /** What Unload on a model's row does: the composition's (1f, 1g, 1k, 1l and 1m fill it). */
  unload: (name: string) => void = () => {}
  /** What "Open models library" does: the composition's. */
  openLibrary: () => void = () => {}

  private binary: string | undefined = undefined
  private openedAt = 0
  private stopTicking: (() => void) | undefined = undefined
  /** The tick of the last good reading, and of the latest read, when it failed. */
  private goodAt: number | undefined = undefined
  private failedAt: number | undefined = undefined

  constructor(
    private readonly runner: CommandRunning,
    private readonly clock: Ticks,
  ) {
    super()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
  }

  /** The monitor opened: read now, and every 5 ticks until it closes. */
  opened(): void {
    if (this.isOpen) return
    this.isOpen = true
    this.openedAt = this.clock.ticks
    this.stopTicking = this.clock.onTick((tick) => {
      if ((tick - this.openedAt) % MonitorViewModel.intervalTicks === 0) void this.read()
    })
    this.changed()
    void this.read()
  }

  /** The monitor closed: no more reads. */
  closed(): void {
    if (!this.isOpen) return
    this.isOpen = false
    this.stopTicking?.()
    this.stopTicking = undefined
    this.changed()
  }

  /** One read of the system. */
  async read(): Promise<void> {
    const at = this.clock.ticks
    if (this.binary === undefined) {
      this.failure = MonitorViewModel.notInstalled
      this.changed()
      return
    }
    const outcome = await this.runner.read(this.binary, ['system'])
    if (!this.isOpen) return
    const document = ReadOutcome.documentOf(outcome)
    if (document) {
      this.reading = new SystemReading(document)
      this.failure = undefined
      this.goodAt = at
      this.failedAt = undefined
      if (this.reading.cpuPercent !== undefined) this.history = [...this.history, this.reading.cpuPercent].slice(-MonitorViewModel.historyLength)
    } else {
      this.failure = ReadOutcome.failure(outcome)
      this.failedAt = at
    }
    this.changed()
  }

  /** What Unload on `name`'s row does. */
  unloadModel(name: string): void {
    this.unload(name)
  }

  /** What "Open models library" does. */
  showLibrary(): void {
    this.openLibrary()
  }

  /** Percent of `part` in `whole`, 0-100, when both are known. */
  static percent(part: number | undefined, whole: number | undefined): number | undefined {
    if (part === undefined || whole === undefined || whole <= 0) return undefined
    return Math.round((part / whole) * 100)
  }

  /** Bytes as the monitor says them: binary units, one decimal, in the unit that shows them; `unknown` when unknown. */
  static size(bytes: number | undefined): string {
    if (bytes === undefined) return 'unknown'
    if (bytes < 1024) return `${bytes} bytes`
    const units = ['KB', 'MB', 'GB', 'TB']
    let value = bytes / 1024
    let unit = 0
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024
      unit += 1
    }
    return `${value.toFixed(1)} ${units[unit]}`
  }

  /** Bytes in GB, one decimal, as the old `gb()` says memory: `26.0`. */
  private static gb(bytes: number): string {
    return (Math.round((bytes / gib) * 10) / 10).toFixed(1)
  }

  /** The stale line, when the latest read failed and a reading stands: how old its value is. */
  private get stale(): string | undefined {
    if (this.reading === undefined || this.failedAt === undefined || this.goodAt === undefined) return undefined
    const seconds = Math.round((this.failedAt - this.goodAt) * 0.4)
    return `reading failed · last value, ${seconds} s old`
  }

  /** Everything the view draws, now. */
  face(): MonitorFace {
    const r = this.reading
    const known = <T>(value: T | undefined, say: (value: T) => string) => (value === undefined ? 'unknown' : say(value))
    const total = r?.memoryTotal
    const used = r?.memoryUsed
    const apogeeBytes = r?.apogeeBytes
    // "Other apps" is used memory minus Apogee's own bytes, so it is known only
    // when both are: with either unknown, it is said unknown and the bar draws
    // no split of used memory, as with nothing read.
    const others = used !== undefined && apogeeBytes !== undefined ? used - apogeeBytes : undefined
    const budget = r?.modelBudget !== undefined ? `models may use ${MonitorViewModel.size(r.modelBudget)}` : `models may use: unknown${r?.modelBudgetUnknown ? ` (${r.modelBudgetUnknown})` : ''}`
    const gpuDetail = r?.gpuName === undefined && r?.gpuUnknown ? `${r.gpuUnknown} · ${budget}` : budget
    const stale = this.stale
    return {
      state: r ? 'reading' : this.failure !== undefined ? 'failed' : 'waiting',
      ...(this.failure !== undefined ? { failure: this.failure } : {}),
      machine: [
        'THIS MAC',
        r?.cpuModel ? r.cpuModel.toUpperCase() : 'CPU UNKNOWN',
        total !== undefined ? `${Math.round(total / gib)} GB` : 'MEMORY UNKNOWN',
      ].join(' · '),
      cpu: { value: known(r?.cpuPercent, (percent) => `${Math.round(percent)}%`), bars: [...this.history] },
      gpu: { value: r?.gpuName ?? 'unknown', detail: gpuDetail, ...(stale !== undefined ? { stale } : {}) },
      memory: {
        value: known(used, MonitorViewModel.gb),
        warning: used !== undefined && total !== undefined && total > 0 && used / total > 0.85,
        apogee: MonitorViewModel.size(apogeeBytes),
        others: MonitorViewModel.size(others),
        free: MonitorViewModel.size(r?.memoryAvailable),
        shares:
          others === undefined || apogeeBytes === undefined
            ? { apogee: 0, others: 0, free: 0 }
            : { apogee: apogeeBytes / gib, others: others / gib, free: (r?.memoryAvailable ?? 0) / gib },
      },
      models: {
        total: MonitorViewModel.size(r?.apogeeBytes),
        rows: (r?.modelsHeld ?? []).map((name) => ({
          name,
          detail: 'held by apogee’s reading process · the CLI reports no size for it',
          size: '',
          action: 'Unload',
        })),
        empty: (r?.modelsHeld.length ?? 0) === 0,
      },
      store: {
        heading: `MODEL STORE · ${r?.store ?? 'unknown'}`,
        total: MonitorViewModel.size(r?.storeBytes),
        rows: [{ label: 'All models', size: MonitorViewModel.size(r?.storeBytes), percent: r?.storeBytes !== undefined ? 100 : 0 }],
        free: `${MonitorViewModel.size(r?.diskAvailable)} free on the store’s volume`,
      },
    }
  }

  /** The sidebar strip's summary: the monitor's numbers; `undefined` before anything was read. */
  get strip(): SystemStrip | undefined {
    const r = this.reading
    if (!r) return this.failure === undefined ? undefined : { summary: this.failure, warning: false, rows: [] }
    const face = this.face()
    const cpu = face.cpu.value
    const memory = r.memoryUsed !== undefined && r.memoryTotal !== undefined ? `${MonitorViewModel.gb(r.memoryUsed)}/${MonitorViewModel.gb(r.memoryTotal)} GB` : 'unknown'
    const stale = face.gpu.stale ? ` · ${Math.round(((this.failedAt ?? 0) - (this.goodAt ?? 0)) * 0.4)} s old` : ''
    // The CLI's words: a one-shot `apogee system` holds no model ("none by this
    // process -- a session holds its own"), so an empty list never says none is loaded.
    const held = `models held: ${r.modelsHeld.length === 0 ? 'none by this process' : r.modelsHeld.join(', ')}`
    const cpuPercent = r.cpuPercent === undefined ? undefined : Math.round(r.cpuPercent)
    const memoryPercent = MonitorViewModel.percent(r.memoryUsed, r.memoryTotal)
    return {
      summary: `mem ${memory} · cpu ${cpu}${stale}`,
      warning: face.memory.warning,
      rows: [
        { label: 'cpu', ...(cpuPercent === undefined ? {} : { percent: cpuPercent }), value: cpu },
        { label: 'mem', ...(memoryPercent === undefined ? {} : { percent: memoryPercent }), value: memory, warning: face.memory.warning },
        { label: 'gpu', value: `${face.gpu.value}${stale}` },
        { label: 'apogee', value: `${face.memory.apogee} · ${held}` },
      ],
    }
  }
}
