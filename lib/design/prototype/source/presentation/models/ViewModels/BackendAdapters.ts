import type { AdapterCheck, BackendAdapter } from '../../../business/reads/Adapters'

/**
 * A backend's adapters as the Models place shows them (the CLI's 53a): each
 * one `config get backends.<name>.adapters` lists beside what `check`'s
 * Adapters row says of it. The Mac app's `AdaptersView`.
 */
export interface AdaptersView {
  readonly backend: string
  readonly rows: ReadonlyArray<{ readonly adapter: BackendAdapter; readonly check: AdapterCheck | undefined }>
  /** Why the doctor's word is missing, in its words. */
  readonly checkFailure: string | undefined
}

/** One adapter as edited: its path, and its scale as typed (empty is 1). */
export interface AdapterEntry {
  readonly path: string
  readonly scale: string
}

/**
 * `config set-backend <name> --adapters <path>[:<scale>],...`: the whole
 * list as the person leaves it, replacing the entry's -- empty clears it.
 * The Mac app's `AdaptersForm`, with the prototype's adapters sheet's words
 * and its sample paths (a browser picks no file for the CLI, as the old
 * sheet's Add a file… does not either).
 */
export class AdaptersForm {
  /** What Add a file… adds: the prototype's own samples. */
  static readonly samples = { gguf: '/Users/someone/Downloads/house-style.lora.gguf', mlx: '/Users/someone/.apogee/training/runs/20261021-051826/adapters' }

  entries: AdapterEntry[]

  constructor(
    readonly backend: string,
    /** `llamacpp` or `mlx`: an mlx entry takes one adapter directory at its trained scale. */
    readonly backendType: string,
    adapters: readonly BackendAdapter[],
  ) {
    this.entries = adapters.map((adapter) => ({ path: adapter.path, scale: String(adapter.scale) }))
  }

  get isMLX(): boolean {
    return this.backendType === 'mlx'
  }

  /** The sheet's words, the prototype's. */
  get intro(): string {
    return this.isMLX ? 'An mlx entry applies one adapter directory, at the scale it was trained at.' : 'GGUF LoRA files, applied over the weights at load and never merged into them -- each at its scale, 1 unless given, up to 2.'
  }

  get addLabel(): string {
    return this.isMLX ? 'Add a directory…' : 'Add a file…'
  }

  get canAdd(): boolean {
    return !(this.isMLX && this.entries.length > 0)
  }

  get applyLabel(): string {
    return this.entries.length > 0 ? 'Apply' : 'Take them all off'
  }

  add(): void {
    if (!this.canAdd) return
    this.entries = [...this.entries, { path: this.isMLX ? AdaptersForm.samples.mlx : AdaptersForm.samples.gguf, scale: '1' }]
  }

  edit(index: number, change: Partial<AdapterEntry>): void {
    this.entries = this.entries.map((entry, at) => (at === index ? { ...entry, ...change } : entry))
  }

  remove(index: number): void {
    this.entries = this.entries.filter((_, at) => at !== index)
  }

  /** `--adapters`' value: each path, `:<scale>` after it when one is typed and is not 1; empty clears. */
  get value(): string {
    return this.entries
      .flatMap((entry) => {
        const path = entry.path.trim()
        if (path === '') return []
        const scale = entry.scale.trim()
        return [scale === '' || scale === '1' || this.isMLX ? path : `${path}:${scale}`]
      })
      .join(',')
  }

  get arguments(): string[] {
    return ['config', 'set-backend', this.backend, '--adapters', this.value]
  }

  /** The command as typed: an empty value written `""`. */
  get command(): string {
    return `apogee config set-backend ${this.backend} --adapters ${this.value === '' ? '""' : this.value}`
  }
}
