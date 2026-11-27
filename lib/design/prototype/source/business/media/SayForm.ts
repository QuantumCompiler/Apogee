/**
 * A speech to say, as the speak card holds it -- and as the command a person
 * would type: `voice say`, every flag the card set and nothing it did not.
 * The Mac app's `VoiceViewModel.plannedArguments` as a form of its own,
 * beside `ClipForm` and `ComposeForm` (design 1m), its argument order the one
 * `engine-command-verdicts.txt` records the CLI taking: `-o OUT -m M --engine
 * E --voice V --speed S -- text`.
 */
export class SayForm {
  /** The voice the CLI speaks with when none is named, where the model has it (`say_text`'s `kDefaultVoice`). */
  static readonly defaultVoice = 'af_heart'
  /** The most a run says, in characters (`voice_say.h`'s `kMaxVoiceCharacters`). */
  static readonly maxCharacters = 4000

  text = ''
  /** A Kokoro voice (`bf_emma`); empty, or the CLI's default, is never named. */
  voice = ''
  /** How fast, 0.5 to 2; 1 is never named. */
  speed = 1
  /**
   * The voice model, by its store handle (`-m`): empty where the store holds
   * one, which the CLI takes as it is -- with more, the CLI asks which rather
   * than guess (its 53m), so the card names one.
   */
  model = ''
  /**
   * `--engine`: set only where the person's model's folder holds Kokoro beside
   * its MLX build (the Mac's `chosenEngine`); anywhere else the model's
   * directory decides, and nothing is passed.
   */
  engine: string | undefined = undefined

  /** A form with `fields` set. */
  static with(fields: Partial<SayForm>): SayForm {
    return Object.assign(new SayForm(), fields)
  }

  /**
   * The arguments after `apogee`, writing to `output`; `undefined` with
   * nothing to say. The text is the last word, after `--`, so the binary
   * never reads it as a flag (the Mac's `Argv.text`, GUI 4g).
   */
  arguments(output: string): string[] | undefined {
    const text = this.text.trim()
    if (text === '') return undefined
    const args = ['voice', 'say', '-o', output]
    const model = this.model.trim()
    if (model !== '') {
      args.push('-m', model)
      if (this.engine !== undefined) args.push('--engine', this.engine)
    }
    const voice = this.voice.trim()
    if (voice !== '' && voice !== SayForm.defaultVoice) args.push('--voice', voice)
    if (this.speed !== 1) args.push('--speed', SayForm.number(this.speed))
    return [...args, '--', text]
  }

  /** A speed as `%g` writes it, the Mac's `String(format: "%g")`: 1.2 is `1.2`, 0.85 is `0.85`. */
  static number(value: number): string {
    return String(Number(value.toPrecision(6)))
  }
}
