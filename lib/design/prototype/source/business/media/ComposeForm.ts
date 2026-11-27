/**
 * A composition to make or to edit, as the command a person would type: the
 * Mac app's `ComposeForm` (`BusinessMedia`, `Projects.swift`), with the two
 * things the design's form sets that the Mac's does not: `--planner` (the
 * CLI's flag, the new composition's Planner) and, on an edit, the settings
 * the new video changes (`--voice`, `-m`, `--music`, `--no-captions`), as the
 * old prototype's edit command draws them (design 1l).
 */
export class ComposeForm {
  brief = ''
  assets: string | undefined = undefined
  music: string | undefined = undefined
  captions = true
  /** A Kokoro voice (`af_heart`); empty is the CLI's default. */
  voice = ''
  /** A pipeline, by its store handle (`-m`); empty is `models.default_video`. */
  model = ''
  /**
   * The voice model, by its store handle (`--voice-model`): empty where the
   * store holds one, which the CLI takes as it is -- with more, the CLI asks
   * which rather than guess (GUI 4f).
   */
  voiceModel = ''
  /** The backend that plans (`--planner`); empty is the chat role's. */
  planner = ''
  /** Editing: the composed video (or its project), the instruction, the scenes to render again on fresh seeds. */
  from: string | undefined = undefined
  instruction = ''
  redo: ReadonlySet<number> = new Set()

  /** A copy, `fields` changed. */
  with(fields: Partial<Pick<ComposeForm, 'brief' | 'assets' | 'music' | 'captions' | 'voice' | 'model' | 'voiceModel' | 'planner' | 'from' | 'instruction' | 'redo'>>): ComposeForm {
    return Object.assign(Object.assign(new ComposeForm(), this), fields)
  }

  /**
   * `video compose …` writing `output`; `dryRun` plans (or, editing, says the
   * diff) and writes nothing. `undefined` while it cannot run. A new
   * composition's brief is the last word, after `--`, so the binary never
   * reads it as a flag (the Mac's `Argv.text`, GUI 4g); an edit's instruction
   * is `--edit`'s value, which the binary takes whole.
   */
  arguments(output: string, dryRun = false): string[] | undefined {
    const args = ['video', 'compose']
    const brief = this.brief.trim()
    if (this.from !== undefined) {
      const instruction = this.instruction.trim()
      if (instruction === '' && this.redo.size === 0) return undefined
      args.push('--from', this.from)
      if (instruction !== '') args.push('--edit', instruction)
      for (const scene of [...this.redo].sort((a, b) => a - b)) args.push('--redo-scene', String(scene))
    } else if (brief === '') return undefined
    args.push('-o', output)
    if (this.from === undefined) {
      if (this.assets !== undefined) args.push('--assets', this.assets)
      if (this.music !== undefined) args.push('--music', this.music)
    } else if (this.music !== undefined) args.push('--music', this.music)
    if (!this.captions) args.push('--no-captions')
    const named = (flag: string, value: string) => {
      if (value.trim() !== '') args.push(flag, value.trim())
    }
    named('--voice-model', this.voiceModel)
    named('--voice', this.voice)
    named('-m', this.model)
    named('--planner', this.planner)
    if (dryRun) args.push('--dry-run')
    return this.from === undefined ? [...args, '--', brief] : args
  }
}
