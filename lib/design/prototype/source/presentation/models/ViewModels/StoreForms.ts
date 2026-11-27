/**
 * The store sheets' forms (design 1h): convert, quantize, register and
 * rename, each the command a person would type. The Mac app's `StoreForms`
 * (`ConvertForm`, `QuantizeForm`, `StoreStep`, `Estimate`,
 * `modelBaseName`), its `StoreViewModel.RegisterForm`, and the prototype's
 * rename sheet (`main.html` at `b375fac9`, section `store sheets`), with the
 * prototype's checks before the CLI is asked: a name a backend already has
 * refuses.
 */

/** One step a store job takes, as its sheet lists it: what it makes, and about how big. */
export interface StoreStep {
  readonly label: string
  /** `about 2.3 GiB`, from bits per weight over the set's stated bytes; an estimate, said as one. */
  readonly estimate: string | undefined
  /** Whether the step makes a set (it ticks as one lands); a registration ticks when the run ends well. */
  readonly makesSet: boolean
  /** The precision a set-making step writes: what its card says beside the size once it is made. */
  readonly level?: string
  /** The backends a registration names: what its card says once it is done. */
  readonly names?: readonly string[]
}

/** A stated size in the CLI's own units: whole KiB and MiB, GiB to a tenth, each truncated as the CLI truncates them. */
export function storeSize(bytes: number): string {
  const b = Math.max(0, Math.floor(bytes))
  if (b < 1024) return `${b} B`
  if (b < 2 ** 20) return `${Math.floor(b / 1024)} KiB`
  if (b < 2 ** 30) return `${Math.floor(b / 2 ** 20)} MiB`
  const tenths = Math.floor((b * 10) / 2 ** 30)
  return `${Math.floor(tenths / 10)}.${tenths % 10} GiB`
}

/** The estimates' arithmetic: bits per weight, against the 16 a SafeTensors set or an F16 GGUF holds. */
export const Estimate = {
  bitsPerWeight: { f16: 16, bf16: 16, f32: 32, q8_0: 8.5, auto: 16, F16: 16, BF16: 16, F32: 32, Q8_0: 8.5, Q6_K: 6.56, Q5_K_M: 5.69, Q4_K_M: 4.85, Q3_K_M: 3.91, Q2_K: 2.63, '4bit': 4.5, '3bit': 3.5, '6bit': 6.5, '8bit': 8.5, mxfp4: 4.25 } as Readonly<Record<string, number>>,

  /** About how big `precision` makes weights that take `bytes` at 16 bits. */
  size(precision: string, bytes: number | undefined): string | undefined {
    const bits = Estimate.bitsPerWeight[precision]
    if (bytes === undefined || bits === undefined) return undefined
    return `about ${storeSize((bytes * bits) / 16)}`
  },
}

/** The base a model's backends are named from: the repository part of an `owner--repo` folder, else the folder (the CLI's `model_base_name`). */
export function modelBaseName(folder: string): string {
  const at = folder.indexOf('--')
  return at >= 0 ? folder.slice(at + 2) : folder
}

/** `<base>-<level>`, the level without its underscores: `Llama3.2-1B-Instruct-Q4KM`. */
function named(base: string, level: string): string {
  return `${base}-${level.replaceAll('_', '')}`
}

/** A step's line on the sheet: its label, then its estimate. */
export function stepLine(step: StoreStep): string {
  return step.estimate ? `${step.label}, ${step.estimate}` : step.label
}

/**
 * `models convert`, as the convert sheet builds it: one command, the CLI's
 * own chain -- convert, each further level, the registrations -- in the one
 * shape the CLI takes it. With `--mlx`, `--register-with` names every
 * precision to convert, the written one first (the CLI's M16); a GGUF
 * registers and quantizes from the F16 alone, so another precision is made
 * on its own (the Mac's M2 verdicts).
 */
export class ConvertForm {
  static readonly ggufPrecisions = ['f16', 'bf16', 'f32', 'q8_0', 'auto']
  static readonly mlxPrecisions = ['4bit', '3bit', '6bit', '8bit', 'mxfp4']
  static readonly ggufLevels = ['Q2_K', 'Q3_K_M', 'Q4_K_M', 'Q5_K_M', 'Q6_K', 'Q8_0']

  /** `-t`: the precision written; `undefined` leaves the CLI's default. */
  precision: string | undefined = undefined
  /** Further levels, each its own backend: a GGUF's quantizations, or MLX's other precisions. */
  levels: string[] = []
  /** `--register`: register what is made. */
  register = true
  /** What each backend's name begins with; the model's own name unless changed. */
  baseName: string
  /** `--model-type` (MLX): the type mlx-lm should take the weights as. */
  modelType = ''

  private constructor(
    readonly handle: string,
    readonly mlx: boolean,
    /** The set's stated bytes, for the estimates. */
    readonly bytes: number | undefined,
  ) {
    this.baseName = ConvertForm.modelBase(handle)
  }

  /** Convert to GGUF…, as the prototype opens it: f16, then Q4_K_M, registered. */
  static gguf(handle: string, bytes: number | undefined): ConvertForm {
    const form = new ConvertForm(handle, false, bytes)
    form.levels = ['Q4_K_M']
    return form
  }

  /** Convert to MLX…: 4bit, registered. */
  static mlx(handle: string, bytes: number | undefined): ConvertForm {
    return new ConvertForm(handle, true, bytes)
  }

  /** The same set, the other target, as the sheet's Make choice switches it. */
  retarget(mlx: boolean): ConvertForm {
    const form = mlx ? ConvertForm.mlx(this.handle, this.bytes) : ConvertForm.gguf(this.handle, this.bytes)
    form.baseName = this.baseName
    return form
  }

  private static modelBase(handle: string): string {
    return modelBaseName(handle.split('/')[0] ?? handle)
  }

  /** The precision the CLI writes when none is named: f16 for a GGUF, 4bit for MLX. */
  get defaultPrecision(): string {
    return this.mlx ? '4bit' : 'f16'
  }

  get precisions(): readonly string[] {
    return this.mlx ? ConvertForm.mlxPrecisions : ConvertForm.ggufPrecisions
  }

  /** What `-t` writes, the CLI's default when none is chosen. */
  get written(): string {
    return this.precision ?? this.defaultPrecision
  }

  /** The further levels offered: GGUF quantizations, or the MLX precisions besides the one written. */
  get levelChoices(): readonly string[] {
    return this.mlx ? ConvertForm.mlxPrecisions.filter((level) => level !== this.written) : ConvertForm.ggufLevels
  }

  /** A GGUF at a precision other than f16, made by itself. */
  get alone(): boolean {
    return !this.mlx && this.written !== 'f16'
  }

  get aloneNote(): string | undefined {
    return this.alone ? `Registering and quantizing start from the F16, so a ${this.written} GGUF is made on its own. Choose f16 to register it or quantize from it.` : undefined
  }

  /** Choosing a level registers what is made: each is its own set and its own backend. */
  get registerForced(): boolean {
    return this.chosen.length > 0
  }

  private get chosen(): string[] {
    return this.levelChoices.filter((level) => this.levels.includes(level))
  }

  private get registers(): boolean {
    return (this.register || this.chosen.length > 0) && !this.alone
  }

  setPrecision(precision: string): void {
    this.precision = precision === this.defaultPrecision ? undefined : precision
    if (this.alone) {
      this.levels = []
      this.register = false
    }
    this.levels = this.levels.filter((level) => this.levelChoices.includes(level))
  }

  toggleLevel(level: string): void {
    this.levels = this.levels.includes(level) ? this.levels.filter((chosen) => chosen !== level) : [...this.levels, level]
    if (!this.mlx && this.levels.length > 0 && this.alone) this.precision = undefined
  }

  /** The command, exactly as the sheet shows it. */
  get arguments(): string[] {
    const out = ['models', 'convert', this.handle]
    if (this.mlx) out.push('--mlx')
    const chosen = this.chosen
    if (this.mlx && chosen.length > 0) {
      out.push('--register-with', [this.written, ...chosen].join(','))
    } else {
      if (this.precision !== undefined) out.push('-t', this.precision)
      if (chosen.length > 0) out.push('--register-with', chosen.join(','))
      else if (this.registers) out.push('--register')
    }
    const base = this.baseName.trim()
    if (this.registers && base !== '' && base !== ConvertForm.modelBase(this.handle)) out.push('--base-name', base)
    const type = this.modelType.trim()
    if (this.mlx && type !== '') out.push('--model-type', type)
    return out
  }

  get command(): string {
    return ['apogee', ...this.arguments].join(' ')
  }

  /** The backends' names, by the CLI's naming: the base, then the level without its underscores, or MLX's precision. */
  get names(): string[] {
    if (!this.registers) return []
    const base = this.baseName.trim()
    return [this.mlx ? this.written : this.written.toUpperCase(), ...this.chosen].map((level) => named(base, level))
  }

  /** The steps the command takes, in its order, each with its estimate. */
  get steps(): StoreStep[] {
    const written = this.mlx ? this.written : this.written.toUpperCase()
    const first = this.mlx ? `Convert to MLX ${written}` : `Convert to GGUF ${written}`
    const steps: StoreStep[] = [{ label: first, estimate: Estimate.size(this.written, this.bytes), makesSet: true, level: written }]
    for (const level of this.chosen) steps.push({ label: this.mlx ? `Convert to MLX ${level}` : `Quantize to ${level}`, estimate: Estimate.size(level, this.bytes), makesSet: true, level })
    const names = this.names
    if (names.length > 0) steps.push({ label: `Register ${names.join(', ')}`, estimate: undefined, makesSet: false, names })
    return steps
  }

  /** What stops the sheet's Convert, in the prototype's words: no beginning named, or a name a backend has. */
  errors(taken: readonly string[]): string[] {
    if (!this.registers) return []
    if (this.baseName.trim() === '') return ['Name the backends: what each name begins with.']
    const clash = this.names.filter((name) => taken.includes(name))
    if (clash.length === 0) return []
    return clash.length === 1 ? [`A backend named ${clash[0]} exists: choose another beginning.`] : [`Backends named ${clash.join(', ')} exist: choose another beginning.`]
  }
}

/**
 * `models quantize`, as the quantize sheet builds it -- and, since the verb
 * takes no `--register`, the `config add-backend` that follows when asked.
 */
export class QuantizeForm {
  static readonly levels = ['Q2_K', 'Q3_K_M', 'Q4_K_M', 'Q5_K_M', 'Q6_K', 'Q8_0']

  level = 'Q4_K_M'
  register = true
  name: string
  private typed = false

  constructor(
    /** The unquantized GGUF, by handle. */
    readonly handle: string,
    /** The model's base name, for the default name. */
    readonly base: string,
    readonly bytes: number | undefined,
  ) {
    this.name = named(base, 'Q4_K_M')
  }

  /** Moves the level, and the name with it until the person has typed one. */
  choose(level: string): void {
    this.level = level
    if (!this.typed) this.name = named(this.base, level)
  }

  rename(name: string): void {
    this.name = name
    this.typed = true
  }

  /** `-t`; Q4_K_M is the CLI's default and is left out. */
  get arguments(): string[] {
    return ['models', 'quantize', this.handle, ...(this.level === 'Q4_K_M' ? [] : ['-t', this.level])]
  }

  /** The command as the sheet shows it: the quantization, then the registration that follows. */
  get command(): string {
    const quantize = ['apogee', ...this.arguments].join(' ')
    return this.register ? `${quantize}\nthen  apogee config add-backend ${this.name.trim() || '…'} --type llamacpp --model-path <the new file>` : quantize
  }

  /** The registration that follows, its file read from what the run made. */
  registration(path: string): string[] | undefined {
    const name = this.name.trim()
    return this.register && name !== '' ? ['config', 'add-backend', name, '--type', 'llamacpp', '--model-path', path] : undefined
  }

  get steps(): StoreStep[] {
    return [{ label: `Quantize to ${this.level}`, estimate: Estimate.size(this.level, this.bytes), makesSet: true, level: this.level }, ...(this.register ? [{ label: `Register ${this.name.trim()}`, estimate: undefined, makesSet: false, names: [this.name.trim()] }] : [])]
  }

  errors(taken: readonly string[]): string[] {
    if (!this.register) return []
    const name = this.name.trim()
    if (name === '') return ['Name the backend.']
    return taken.includes(name) ? [`A backend named ${name} exists: choose another name.`] : []
  }
}

/** `config add-backend`, as the register sheet builds it: the Mac's `StoreViewModel.RegisterForm`. */
export class RegisterForm {
  static readonly caches = ['f16', 'q8_0', 'q4_0']
  static readonly windows = ['8192', '16384', '32768', '65536', '131072']

  /** `--context-size`; empty leaves the model's own. */
  window = ''
  /** `--cache-type`; the CLI's default, q8_0, is left out. */
  cache = 'q8_0'

  constructor(
    readonly handle: string,
    /** `llamacpp` or `mlx`, by the set's format. */
    readonly type: string,
    readonly path: string,
    /** The name `config scan` lists it under. */
    public name: string,
    /** `--mmproj-path`, carried when the set has a projector. */
    readonly projector: string | undefined,
    /** The model's default window, as its `models info` states it. */
    readonly defaultWindow: number | undefined,
  ) {}

  get arguments(): string[] {
    const out = ['config', 'add-backend', this.name.trim() || '…', '--type', this.type, '--model-path', this.path]
    if (this.projector) out.push('--mmproj-path', this.projector)
    if (this.window !== '') out.push('--context-size', this.window)
    if (this.type === 'llamacpp' && this.cache !== 'q8_0') out.push('--cache-type', this.cache)
    return out
  }

  get command(): string {
    return ['apogee', ...this.arguments].join(' ')
  }

  /** The window menu: the model's default first, as its document states it. */
  get windowChoices(): ReadonlyArray<{ readonly value: string; readonly label: string }> {
    const fallback = this.defaultWindow === undefined ? 'the model’s default' : `the model’s default, ${this.defaultWindow.toLocaleString('en-US')} here`
    return [{ value: '', label: fallback }, ...RegisterForm.windows.map((value) => ({ value, label: Number(value).toLocaleString('en-US') }))]
  }

  errors(taken: readonly string[]): string[] {
    const name = this.name.trim()
    if (name === '') return ['Name the backend.']
    return taken.includes(name) ? [`A backend named ${name} exists: choose another name.`] : []
  }
}

/** `models rename <model> <new>`, as the rename sheet builds it: its backends named after it follow, as the CLI does it. */
export class RenameForm {
  name: string

  constructor(
    readonly model: string,
    readonly sets: number,
    /** Every backend that runs from the model's sets. */
    readonly backends: readonly string[],
  ) {
    this.name = model
  }

  /** The backends named after it, `<base>-…`, renamed with it. */
  get follow(): string[] {
    const base = modelBaseName(this.model)
    return this.backends.filter((backend) => backend.startsWith(`${base}-`))
  }

  get arguments(): string[] {
    return ['models', 'rename', this.model, this.name.trim() || '…']
  }

  get command(): string {
    return ['apogee', ...this.arguments].join(' ')
  }

  errors(models: readonly string[]): string[] {
    const name = this.name.trim()
    if (name === '') return ['Give it a name.']
    if (name !== this.model && models.includes(name)) return [`A model named ${name} is in the store already.`]
    if (/[/\s]/.test(name)) return ['A name has no spaces or slashes.']
    return []
  }

  canRename(models: readonly string[]): boolean {
    return this.errors(models).length === 0 && this.name.trim() !== this.model
  }
}
