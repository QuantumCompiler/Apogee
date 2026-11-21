import { ClipRecord } from './MediaRecords'

/** What happens: a new clip, a recorded run again (`--from <record>`), or a clip continued (`--extend <clip>`). */
export type ClipStart = { readonly case: 'new' } | { readonly case: 'again'; readonly record: string } | { readonly case: 'extend'; readonly clip: string }

/** `--draft` (16 steps, the same seed) or `--final` (50 steps). */
export type ClipPreset = 'none' | 'draft' | 'final'

/**
 * A clip to make, as the generate form holds it -- and as the command a
 * person would type: `video generate`, every flag the form set and nothing
 * it did not. The Mac app's `ClipForm` (`BusinessMedia`, `ClipForm.swift`),
 * its argument order the one `engine-command-verdicts.txt` records the CLI
 * taking: `-o OUT -m M --engine E … -- prompt`.
 */
export class ClipForm {
  start: ClipStart = { case: 'new' }
  prompt = ''
  image: string | undefined = undefined
  /** A pipeline: a store handle, id or model; empty is the default video pipeline. */
  model = ''
  seed: number | undefined = undefined
  duration: number | undefined = undefined
  /** `WIDTHxHEIGHT`. */
  resolution: string | undefined = undefined
  steps: number | undefined = undefined
  fps: number | undefined = undefined
  preset: ClipPreset = 'none'
  /** Each a `.safetensors` file (or a folder holding one), its strength after a colon. */
  loras: string[] = []
  /** No LoRAs: with `--from`, the record's cleared. */
  noLoRAs = false
  /**
   * `--engine`: set only where the person chose the engine -- a model folder
   * holding a pipeline beside its MLX conversion (the GUI's 3b). Anywhere
   * else the model's directory decides, and nothing is passed (the CLI's 53l).
   */
  engine: string | undefined = undefined

  /** A form with `fields` set. */
  static with(fields: Partial<ClipForm>): ClipForm {
    return Object.assign(new ClipForm(), fields)
  }

  /**
   * The arguments after `apogee`, writing to `output`; `undefined` while the
   * form cannot run -- a new clip needs a prompt or a seed image. The prompt
   * is the last word, after `--`, so the binary never reads it as a flag.
   */
  arguments(output: string): string[] | undefined {
    const prompt = this.prompt.trim()
    const args = ['video', 'generate']
    switch (this.start.case) {
      case 'new':
        if (prompt === '' && this.image === undefined) return undefined
        break
      case 'again':
        args.push('--from', this.start.record)
        break
      case 'extend':
        args.push('--extend', this.start.clip)
        break
    }
    args.push('-o', output)
    if (this.image !== undefined) args.push('--image', this.image)
    const model = this.model.trim()
    if (model !== '') args.push('-m', model)
    if (this.engine !== undefined) args.push('--engine', this.engine)
    if (this.seed !== undefined) args.push('--seed', String(this.seed))
    if (this.duration !== undefined) args.push('--duration', ClipForm.number(this.duration))
    if (this.resolution !== undefined && this.resolution !== '') args.push('--resolution', this.resolution)
    if (this.steps !== undefined && this.preset === 'none') args.push('--steps', String(this.steps))
    if (this.fps !== undefined) args.push('--fps', String(this.fps))
    if (this.preset === 'draft') args.push('--draft')
    if (this.preset === 'final') args.push('--final')
    if (this.noLoRAs) args.push('--no-lora')
    else for (const lora of this.loras) if (lora !== '') args.push('--lora', lora)
    return prompt === '' ? args : [...args, '--', prompt]
  }

  /** `5` for 5.0, `2.5` for 2.5. */
  static number(value: number): string {
    // JavaScript already writes 5.0 as `5`; the Mac's needed its own rule.
    return String(value)
  }

  /** The form for running `record` again, or continuing its clip. */
  static from(record: ClipRecord, extend: boolean): ClipForm {
    return ClipForm.with({ start: extend ? { case: 'extend', clip: record.output } : { case: 'again', record: ClipRecord.sidecarOf(record) } })
  }
}
