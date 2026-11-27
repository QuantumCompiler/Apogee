import { JSONValue } from '../../../data/protocol/JSONValue'

/**
 * `train promote <run> --as <backend>`, the gate honoured unless forced: the
 * Mac's `PromoteForm` (TrainingViewModel.swift:94-157). Merged -- fused,
 * converted, registered -- or, with `--adapter` (the CLI's 53b), the run's
 * adapter alone on an existing entry. Each mode carries only the flags the
 * CLI takes in it: a merge's `--target`, `--quantize` and `--keep-fused`, an
 * adapter gate's `--suite` and `--judge` (53c).
 */
export class PromoteForm {
  run = ''
  backend = ''
  /** `gguf` or `mlx`; undefined lets the CLI choose by what `--as` runs. */
  target: string | undefined = undefined
  /** A GGUF level; undefined keeps F16. */
  quantize: string | undefined = undefined
  force = false
  adapter = false
  suite = ''
  judge: string | undefined = undefined
  trainer: string | undefined = undefined
  keepFused = false

  /** The command as a person types it, after `apogee`; undefined without a run and a backend. */
  arguments(): string[] | undefined {
    const backend = this.backend.trim()
    if (this.run === '' || backend === '') return undefined
    return this.argumentsAs(backend)
  }

  /** The same with `backend` in place of the typed one: the sheet's line says `<backend>` until one is named. */
  argumentsAs(backend: string): string[] {
    const args = ['train', 'promote', this.run, '--as', backend]
    if (this.adapter) {
      args.push('--adapter')
      const suite = this.suite.trim()
      if (suite !== '') args.push('--suite', suite)
      if (this.judge !== undefined) args.push('--judge', this.judge)
    } else {
      if (this.target !== undefined) args.push('--target', this.target)
      if (this.quantize !== undefined) args.push('--quantize', this.quantize)
      if (this.keepFused) args.push('--keep-fused')
    }
    if (this.trainer !== undefined) args.push('--trainer', this.trainer)
    if (this.force) args.push('--force')
    return args
  }

  /** A copy, so a refusal keeps the form as it was sent. */
  copy(): PromoteForm {
    return Object.assign(new PromoteForm(), this)
  }

  /**
   * The form the CLI's refusal points at: the other mode where the refusal
   * names it as the way -- `promote into it with --adapter`, or `without
   * --adapter` -- else this one, to change as the words say.
   */
  door(refusal: string): PromoteForm {
    const form = this.copy()
    if (!this.adapter && refusal.includes('with --adapter')) form.adapter = true
    else if (this.adapter && refusal.includes('without --adapter')) form.adapter = false
    return form
  }

  static readonly levels = ['Q2_K', 'Q3_K_M', 'Q4_K_M', 'Q5_K_M', 'Q6_K', 'Q8_0', 'F16'] as const
  /** `--trainer`'s choices, as the CLI lists them. */
  static readonly trainers = ['auto', 'mlx', 'peft', 'mock'] as const
}

/** What the promote sheet changes: any of the form's fields. */
export type PromoteChange = Partial<Pick<PromoteForm, 'backend' | 'target' | 'quantize' | 'force' | 'adapter' | 'suite' | 'judge' | 'trainer' | 'keepFused'>>

/** A promotion the CLI refused or failed: the form as it was sent, and the CLI's words, which the sheet opens again with. */
export interface RefusedPromotion {
  readonly form: PromoteForm
  readonly refusal: string
}

/**
 * `datasets pull owner/name` finished: where the CLI put the files and what
 * it said of them -- `<n> file(s), <size>` -- for the `datasets prepare` the
 * banner offers. The Mac's `PulledDataset`.
 */
export interface PulledDataset {
  /** The folder `datasets prepare` takes, as the CLI named it. */
  readonly path: string
  /** `2 file(s), 136 KiB`, the pull's own count line. */
  readonly summary: string
}

export const PulledDataset = {
  /** From what a pull printed as text: its `Convert it with:  apogee datasets prepare <path>` line, and the count line before it. */
  fromSaid(lines: readonly string[]): PulledDataset | undefined {
    const door = 'apogee datasets prepare '
    const line = [...lines].reverse().find((said) => said.includes(door))
    if (line === undefined) return undefined
    return { path: line.slice(line.indexOf(door) + door.length).trim(), summary: countLine(lines) ?? '' }
  },

  /** From a streamed pull's closing document (the CLI's M30), `{object: "dataset.pull", ref, path, files, bytes}`. */
  fromDocument(document: JSONValue, lines: readonly string[]): PulledDataset | undefined {
    const value = JSONValue.object(document)
    const path = JSONValue.string(value?.path)
    if (JSONValue.string(value?.object) !== 'dataset.pull' || path === undefined || path === '') return undefined
    const files = JSONValue.int(value?.files) ?? 0
    return { path, summary: countLine(lines) ?? `${files} file(s)` }
  },
}

/** The pull's own count line, `2 file(s), 136 KiB` -- never its opening `downloading …` line. */
function countLine(lines: readonly string[]): string | undefined {
  return [...lines].reverse().find((line) => line.includes(' file(s), ') && !line.startsWith('downloading '))
}
