import { JobProgress } from '../../../business/jobs/JobProgress'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { MediaEngine, MediaModel } from '../../../business/media/MediaEngine'
import { MediaLibrary, SpeechRecord, baseName } from '../../../business/media/MediaRecords'
import type { FolderReading, Skipped } from '../../../business/media/MediaRecords'
import { SayForm } from '../../../business/media/SayForm'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { Field } from '../../../business/reads/Models'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import type { Clock } from '../../../infrastructure/clock/Clock'
import type { MediaPreferences } from '../../styles/Media/MediaPreferences'
import { secs, stemOf, tilde, when } from '../../styles/Media/MediaWords'
import type { Playback } from '../../styles/Media/Playback'
import { Observable } from '../../styles/Observation/Observable'
import type { SpeakCardFace, SpeechAction, SpeechPageFace, SpeechRowFace, VoiceFace, WaveBarFace } from './VoiceFaces'

export type { SpeechAction }

/** Where speeches are written and read: `~/Music/Apogee`, the Mac's `outputFolder` (the stand-in's home). */
export const musicFolder = '/Users/someone/Music/Apogee'

/** What the place needs of the clock: its ticks (the player's), its fixed now (a record's age), and each tick. */
export type VoiceClock = Pick<Clock, 'ticks' | 'now' | 'onTick'>

/** The voices of one language, as the model lists them: the Mac's `VoiceGroup`. */
export interface VoiceGroup {
  readonly language: string
  readonly voices: readonly string[]
}

/** A run of `voice say`: its arguments, what it writes, what it says, and what it has said. */
interface SpeechRun {
  readonly args: readonly string[]
  readonly output: string
  readonly text: string
  readonly voice: string
  /** The voice model it speaks with, by its store handle; `undefined` where the store names none. */
  readonly model: string | undefined
  readonly progress: JobProgress
  job: JobRunning | undefined
}

/** The waveform's bars, as the old prototype draws them. */
const barCount = 96

/** The CLI's refusal with no voice model in the store (`voice_say.cpp`'s `resolve_voice_model`). */
const noVoiceModel = "no voice model in the store -- 'apogee models pull hexgrad/Kokoro-82M' stores the reference one"

/** A speed as the old prototype says it: 1 is `1.0×`, 1.1 `1.1×`, 1.25 `1.25×`. */
function speedText(speed: number): string {
  return `${(Math.round(speed * 100) / 100).toFixed(2).replace(/0$/, '')}×`
}

/** The old prototype's `splitSentences`: where `.`, `!` or `?` meets a space. */
function splitSentences(text: string): string[] {
  return text
    .trim()
    .replace(/([.!?])\s+/g, '$1\u0001')
    .split('\u0001')
    .map((sentence) => sentence.trim())
    .filter((sentence) => sentence !== '')
}

/** A text in the old command's quotes. */
const quoted = (text: string) => `"${text.replace(/"/g, '\\"')}"`

/** `-- text` kept last: the 52b stream asked for before `--`, as the Mac's `Argv.adding` puts it. */
function streaming(args: readonly string[]): string[] {
  const at = args.indexOf('--')
  return at < 0 ? [...args, '--output-format', 'stream-json'] : [...args.slice(0, at), '--output-format', 'stream-json', ...args.slice(at)]
}

/**
 * The Voice place (design 1m): the Mac app's `VoiceViewModel`, its names,
 * drawn as the old prototype's place -- the speeches list with the run, the
 * speak card inline (the Mac's say sheet stays Mac-only), and the selected
 * speech with its waveform, the one player, the sentence table with the
 * record's own timings, its facts and the command that says it again -- over
 * the stand-in.
 *
 * The library is records, not a database: the `voice.speech` documents the
 * CLI wrote beside its WAVs in `~/Music/Apogee`, read through a
 * `FolderReading` on coming into view and again when a run ends, as the Mac
 * reads them (the stand-in's `writes` puts a run's record there). The voices
 * are the voice model's own, from its `models info`; the timings are the
 * record's, never estimated again. A run is the command a person would type,
 * a job on the shared job list, so the sidebar and the monitor see it. The
 * chosen voice is the window's `MediaPreferences`: the card starts from it,
 * and "Use this voice for compositions" sets it and an open new
 * composition's. It draws no install and picks no model (the document's
 * defaults: the Mac's say sheet, model picker, folder scan and runtime
 * install stay Mac-only). Plain TypeScript: it never imports React.
 */
export class VoiceViewModel extends Observable {
  /** The speeches: the folder's records, newest first. */
  speeches: SpeechRecord[] = []
  skipped: Skipped[] = []
  /** The speech shown, by its WAV's path. */
  selected: string | undefined = undefined
  /** The store's voice models, as `models list` states them, PyTorch first. */
  models: MediaModel[] = []
  /** The voice model the card speaks with, by its store handle: the store's first. */
  voiceModel: string | undefined = undefined
  voices: VoiceGroup[] = []
  text = ''
  voice: string
  speed = 1
  /** Save as, as typed; `undefined` is the card's own name for the file. */
  out: string | undefined = undefined

  /** The toast. */
  say: ((text: string) => void) | undefined = undefined
  /** Models' Stored weights at a set: the header's model line. */
  openModels: ((handle: string) => void) | undefined = undefined

  private binary: string | undefined = undefined
  /** Why `models list` gave no document; `undefined` once read, or before. */
  private storeFailure: string | undefined = undefined
  private storeRead = false
  /** The speech being spoken, or the last one that failed, until the next Speak. */
  private current: SpeechRun | undefined = undefined
  /** New speech pressed: nothing shown until a speech is picked or made. */
  private showingNone = false

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
    private readonly folders: FolderReading,
    private readonly clock: VoiceClock,
    /** The one player of the app's media, shared with Video and Compose (design 1k). */
    readonly playback: Playback,
    /** The voice new speech starts from, the window's preferences (design 1l). */
    private readonly media: MediaPreferences,
  ) {
    super()
    this.voice = media.voice
    this.playback.subscribe(() => this.changed())
    clock.onTick(() => {
      if (this.playback.on) this.changed()
    })
    // Settings' choice heard: drawn again. The card keeps its own voice, as the old `vf` keeps it, until New speech.
    media.subscribe(() => this.changed())
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    this.refresh()
  }

  /** Coming into view: the speeches read again, and the voice model's voices. */
  async appeared(): Promise<void> {
    this.refresh()
    await this.readVoices()
  }

  /** The records, read again from the folder: a speech gone is no longer shown; with none shown, the newest is. */
  refresh(): void {
    const scan = MediaLibrary.speeches([musicFolder], this.folders)
    this.speeches = scan.records
    this.skipped = scan.skipped
    if (this.selected !== undefined && !this.speeches.some((speech) => speech.output === this.selected)) this.selected = undefined
    if (this.selected === undefined && !this.showingNone) this.selected = this.speeches[0]?.output
    this.changed()
  }

  /**
   * The store's voice models (`models list`'s voice rows and its MLX voice
   * models, PyTorch first) and the first one's voices: its `models info`'s
   * `voices` field, one language a line -- as the Mac reads them.
   */
  async readVoices(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['models', 'list'])
    const list = ReadOutcome.documentOf(outcome)
    this.storeRead = list !== undefined
    this.storeFailure = ReadOutcome.failure(outcome)
    this.models = list ? MediaModel.voiceModels(list) : []
    this.voiceModel = this.models[0]?.handle
    this.voices = []
    if (this.voiceModel !== undefined) {
      const info = ReadOutcome.documentOf(await this.runner.read(binary, ['models', 'info', this.voiceModel]))
      this.voices = VoiceViewModel.groups(info ? (Field.list(info).find((field) => field.label === 'voices')?.value ?? '') : '')
    }
    // A voice the model does not carry goes to the one the CLI speaks with unnamed: its default, else its first.
    const all = this.voices.flatMap((group) => group.voices)
    if (all.length > 0 && !all.includes(this.voice)) this.voice = all.includes(SayForm.defaultVoice) ? SayForm.defaultVoice : (all[0] ?? '')
    this.changed()
  }

  /** `American English: af_heart, am_adam` lines, as the model lists them (the Mac's `voices(in:)`). */
  static groups(text: string): VoiceGroup[] {
    return text.split('\n').flatMap((line) => {
      const colon = line.indexOf(':')
      if (colon < 0) return []
      const voices = line
        .slice(colon + 1)
        .split(',')
        .map((name) => name.trim())
        .filter((name) => name !== '')
      return voices.length === 0 ? [] : [{ language: line.slice(0, colon).trim(), voices }]
    })
  }

  /** The model the card speaks with, as the store lists it. */
  get chosen(): MediaModel | undefined {
    return this.models.find((model) => model.handle === this.voiceModel)
  }

  /** `--engine`, where the person's model's folder holds Kokoro beside its MLX build (the Mac's `chosenEngine`): anywhere else the directory decides. */
  get chosenEngine(): MediaEngine | undefined {
    const chosen = this.chosen
    return this.models.length > 1 && chosen && MediaModel.choosesEngine(chosen, this.models) ? chosen.engine : undefined
  }

  /** What an engine speaks with, in the Mac's words (its `speaksOn`). */
  static speaksOn(engine: MediaEngine): string {
    return engine === 'torch' ? 'PyTorch, through Kokoro: the voice model as Kokoro publishes it' : engine === 'mlx' ? 'MLX, through mlx-audio: an MLX voice model' : engine
  }

  /** The card's engine line, the Mac's `engineLine`: what the speech will be spoken on, as the model's directory says; `undefined` with no voice model. */
  get engineLine(): string | undefined {
    const chosen = this.chosen
    if (!chosen) return undefined
    const engine = this.chosenEngine
    return `Speaks on ${VoiceViewModel.speaksOn(chosen.engine)}.${engine === undefined ? '' : ` ${MediaModel.folder(chosen)} holds it and its MLX build, so the engine is yours: --engine ${engine}.`}`
  }

  /** The header's model line: the voice model in the store. */
  openModel(): void {
    if (this.voiceModel !== undefined) this.openModels?.(this.voiceModel)
  }

  // MARK: - The speeches

  select(output: string): void {
    this.selected = output
    this.showingNone = false
    this.changed()
  }

  get selectedSpeech(): SpeechRecord | undefined {
    return this.selected === undefined ? undefined : this.speeches.find((speech) => speech.output === this.selected)
  }

  /** The player's key for a speech. */
  private key(speech: SpeechRecord): string {
    return `speech:${speech.output}`
  }

  /** Where the player stands in the speech shown, in seconds. */
  get position(): number {
    const speech = this.selectedSpeech
    return speech ? this.playback.position(this.key(speech), SpeechRecord.duration(speech)) : 0
  }

  /** The sentence at the player's position, by the record's own timings (the Mac's `currentSentence`). */
  get currentSentence(): number | undefined {
    const speech = this.selectedSpeech
    return speech ? SpeechRecord.sentence(speech, this.position) : undefined
  }

  /** Play or pause the speech shown. */
  toggle(): void {
    const speech = this.selectedSpeech
    if (speech) this.playback.toggle(this.key(speech), SpeechRecord.duration(speech))
  }

  /** Play the speech shown from its sentence `index` (0-based): where the record says it starts. */
  seek(index: number): void {
    const speech = this.selectedSpeech
    const at = speech?.sentences[index]?.offset
    if (speech && at !== undefined) this.playback.seek(this.key(speech), at)
  }

  /** The speech shown paused `at` seconds in: where a preset's frame stands. */
  hold(at: number): void {
    const speech = this.selectedSpeech
    if (speech) this.playback.hold(this.key(speech), at)
  }

  /** A selected speech's action, as the old buttons do. */
  act(action: SpeechAction): void {
    const speech = this.selectedSpeech
    if (!speech) return
    switch (action) {
      case 'settings':
        this.text = speech.text
        this.voice = speech.voice
        this.speed = speech.speed ?? 1
        this.out = undefined
        this.changed()
        this.say?.('The text, voice and speed are in the editor above.')
        return
      case 'compositions':
        // The default and an open new composition's voice together, as the old button sets `defaultVoice` and `nc.voice` (main.html 7692): never Settings' `chooseVoice`.
        this.media.useForCompositions(speech.voice)
        this.say?.(`${speech.voice} is now the voice for new speech and compositions`)
        return
      case 'finder':
        this.say?.(`Revealed ${tilde(speech.output)} in Finder (prototype)`)
        return
    }
  }

  // MARK: - The speak card

  /** New speech: the card emptied, from the chosen voice, and no speech shown (the old `newSpeech`). */
  newSpeech(): void {
    this.text = ''
    this.voice = this.media.voice
    this.speed = 1
    this.out = undefined
    this.selected = undefined
    this.showingNone = true
    this.changed()
  }

  setText(text: string): void {
    this.text = text
    this.changed()
  }

  pickVoice(voice: string): void {
    this.voice = voice
    this.changed()
  }

  setSpeed(speed: number): void {
    this.speed = speed
    this.changed()
  }

  resetSpeed(): void {
    this.setSpeed(1)
  }

  setOut(out: string): void {
    this.out = out
    this.changed()
  }

  /** Every name `~/Music/Apogee` holds (the old `spNames`): a run's WAV lands only when it finishes, so the card keeps its name until then. */
  private get taken(): string[] {
    return this.folders.files(musicFolder).map((file) => file.name)
  }

  /** The file the card writes: as typed, trimmed, else a free name from its first words (the old `vOut`). */
  get output(): string {
    return this.out !== undefined ? this.out.trim() : MediaLibrary.freeName(this.taken, stemOf(this.text), 'wav')
  }

  /** Why the card cannot speak, in the old prototype's words. */
  get errors(): string[] {
    const errors: string[] = []
    const length = this.text.length
    if (length > SayForm.maxCharacters) errors.push(`The text is 4,000 characters at most; this is ${length.toLocaleString('en-US')}.`)
    const out = this.out?.trim()
    if (out === '') errors.push('Name the file to write.')
    else if (out !== undefined && (this.taken.includes(out) || this.taken.includes(`${out}.json`))) errors.push(`~/Music/Apogee/${out} exists. A speech never writes over a file; name another.`)
    return errors
  }

  /** The card as the say form: the voice model named only where the store holds more than one, its engine only where the person's folder holds two. */
  private get form(): SayForm {
    return SayForm.with({ text: this.text, voice: this.voice, speed: this.speed, model: this.models.length > 1 ? (this.voiceModel ?? '') : '', engine: this.chosenEngine })
  }

  /** `voice say -o <wav> … -- <text>`, the Mac's `plannedArguments`: `undefined` with nothing to say. */
  get plannedArguments(): string[] | undefined {
    return this.form.arguments(`${musicFolder}/${this.output}`)
  }

  /** The run's progress, the Mac's `run`: the speech being spoken, or the last one that failed, until the next Speak. */
  get run(): JobProgress | undefined {
    return this.current?.progress
  }

  /** Whether a speech is being spoken now. */
  get isRunning(): boolean {
    return this.current?.job !== undefined
  }

  /** The speech being spoken now, as the monitor lists it: the arguments its job runs, its voice model speaking, and the device it said it is on. */
  get speaking(): { readonly arguments: readonly string[]; readonly name: string; readonly runtime: string | undefined } | undefined {
    const run = this.current
    if (!run?.job) return undefined
    return { arguments: run.args, name: run.model === undefined ? 'apogee voice say' : `${run.model.split('/')[0] ?? run.model} · speaking`, runtime: run.progress.ready?.device }
  }

  /** Speak: the card's command, supervised -- one at a time. */
  speak(): void {
    const args = this.plannedArguments
    const binary = this.binary
    if (args === undefined || this.errors.length > 0 || this.isRunning || binary === undefined) return
    const streamed = streaming(args)
    const job = this.starter.startJob(binary, streamed, true)
    const run: SpeechRun = { args: streamed, output: `${musicFolder}/${this.output}`, text: this.text.trim(), voice: this.voice, model: this.voiceModel, progress: new JobProgress(), job }
    this.current = run
    this.changed()
    void (async () => {
      for await (const update of job.updates) {
        run.progress.apply(update)
        this.changed()
      }
      run.job = undefined
      this.finished(run)
    })()
  }

  private finished(run: SpeechRun): void {
    const status = run.progress.status
    if (status.case === 'done') {
      // The record is beside its WAV now: the folder read again, the new speech shown, as the Mac does.
      this.refresh()
      const made = this.speeches.find((speech) => speech.output === run.output)
      if (made) this.select(made.output)
      if (this.current === run) this.current = undefined
      this.out = undefined
      this.say?.(`${baseName(run.output)} is ready · ${secs(made ? SpeechRecord.duration(made) : 0)} s, measured from its header`)
    } else if (status.case === 'stopped') {
      if (this.current === run) this.current = undefined
      this.say?.(`cancelled -- nothing was written (${run.output} untouched)`)
    }
    // A failure stays, said under Speak, until the next Speak.
    this.changed()
  }

  // MARK: - The face

  face(): VoiceFace {
    const run = this.current
    const running = this.isRunning
    const list: SpeechRowFace[] = []
    if (run && running) list.push({ id: run.output, file: baseName(run.output), text: run.text, meta: `speaking · ${run.voice}`, running: true, current: false })
    for (const speech of this.speeches) {
      list.push({ id: speech.output, file: speech.name, text: speech.text, meta: `${speech.voice} · ${speedText(speech.speed ?? 1)} · ${secs(SpeechRecord.duration(speech))} s`, running: false, current: speech.output === this.selected })
    }
    const chosen = this.chosen
    return {
      modelLine: chosen ? MediaModel.label(chosen, this.models) : this.binary === undefined ? 'apogee is not installed' : this.storeRead ? 'no voice model in the store' : 'the store could not be read',
      modelTitle: chosen ? `Show ${MediaModel.folder(chosen)} in the library` : '',
      canOpenModel: chosen !== undefined,
      list,
      empty: list.length === 0 ? 'Nothing spoken yet.' : '',
      skipped: this.skipped.map((skip) => `${baseName(skip.file)}: ${skip.reason}`),
      card: this.cardFace(),
      speech: this.speechFace(),
    }
  }

  private cardFace(): SpeakCardFace {
    const run = this.current
    const running = this.isRunning
    const errors = this.errors
    const length = this.text.length
    const sentences = splitSentences(this.text)
    const form = this.form
    const command = [`apogee voice say ${quoted(this.text.trim() || '…')}`]
    if (form.model !== '') command.push(`-m ${form.model}${form.engine === undefined ? '' : ` --engine ${form.engine}`}`)
    if (this.voice !== '' && this.voice !== SayForm.defaultVoice) command.push(`--voice ${this.voice}`)
    if (this.speed !== 1) command.push(`--speed ${SayForm.number(this.speed)}`)
    command.push(`-o ~/Music/Apogee/${this.output}`)
    const failed = run && !running && run.progress.status.case === 'failed' ? run.progress.status.message : undefined
    const spoken = run ? splitSentences(run.text).length : 0
    const device = run?.progress.ready?.device
    return {
      text: this.text,
      count: `${length.toLocaleString('en-US')} / 4,000`,
      countColor: length > SayForm.maxCharacters ? 'var(--status-error)' : 'var(--text-tertiary)',
      sentencesText: sentences.length ? `${sentences.length} ${sentences.length === 1 ? 'sentence' : 'sentences'} · split where . ! or ? meets a space, never by a model` : 'Sentences are split where . ! or ? meets a space, never by a model.',
      groups: this.voices.map((group) => ({
        title: `${group.language.toUpperCase()} · ${group.voices.length}`,
        voices: group.voices.map((id) => ({ id, hint: id === SayForm.defaultVoice ? 'default' : '', title: `${id}, ${id[1] === 'f' ? 'a female voice' : 'a male voice'}`, current: id === this.voice })),
      })),
      speed: this.speed,
      speedText: speedText(this.speed),
      speedChanged: this.speed !== 1,
      out: this.out ?? this.output,
      errors,
      command: command.join(' '),
      cantSpeak: this.text.trim() === '' || errors.length > 0 || running || this.binary === undefined,
      speaking: running,
      speakingText: run && running ? `speaking ${spoken} ${spoken === 1 ? 'sentence' : 'sentences'} as ${run.voice}${device ? ` on the ${device}` : ''}` : '',
      note: running ? '' : failed !== undefined ? failed : this.binary === undefined ? 'apogee is not installed' : !this.storeRead ? (this.storeFailure ?? '') : this.engineLine !== undefined ? `${this.engineLine} Nothing leaves this Mac.` : noVoiceModel,
      noteColor: failed !== undefined ? 'var(--status-error)' : 'var(--text-tertiary)',
    }
  }

  /** Nothing selected: the page drawn empty, said so by `shown`. */
  private static readonly noSpeech: SpeechPageFace = {
    shown: false,
    file: '',
    meta: '',
    actions: [],
    bars: [],
    player: { aspect: '1 / 1', dims: '', badge: '', playing: false, pct: 0, time: '', right: '', marks: [], label: '', caption: '', toggleLabel: 'Play' },
    format: '',
    sentences: [],
    measured: '',
    record: '',
    facts: [],
    command: '',
  }

  private speechFace(): SpeechPageFace {
    const speech = this.selectedSpeech
    if (!speech) return VoiceViewModel.noSpeech
    const key = this.key(speech)
    const total = SpeechRecord.duration(speech)
    const position = this.position
    const playing = this.playback.playing(key, total)
    // Lit while it plays, as the old row lights `curS`; paused, the waveform says how far.
    const lit = playing ? this.currentSentence : undefined
    const bars: WaveBarFace[] = []
    for (let bar = 0; bar < barCount; bar += 1) {
      const at = ((bar + 0.5) / barCount) * total
      // The old margins of silence at a sentence's edges (0.08 s, 0.12 s), scaled to a sentence too short for them.
      const index = speech.sentences.findIndex((sentence) => at >= sentence.offset + Math.min(0.08, sentence.seconds * 0.2) && at < sentence.offset + sentence.seconds - Math.min(0.12, sentence.seconds * 0.3))
      const code = speech.text.charCodeAt((bar * 7) % speech.text.length) || 60
      const height = index < 0 ? 4 : Math.round(22 + 70 * Math.abs(Math.sin(bar * 1.73 + code * 0.11)) * (0.6 + 0.4 * Math.abs(Math.sin(bar / 5))))
      bars.push({ height, color: index >= 0 && index === lit ? 'var(--status-accent)' : at <= position ? 'var(--text-secondary)' : 'var(--line-strong)' })
    }
    const ran = Object.entries(speech.versions)
      .filter(([name, version]) => name !== 'apogee' && version !== '')
      .sort(([a], [b]) => (a < b ? -1 : 1))
      .map(([name, version]) => `${name.replaceAll('_', '-')} ${version}`)
    const slash = speech.model.indexOf('/')
    const command = [`apogee voice say ${quoted(speech.text)}`]
    // Its model named unless the store's one voice model is it: what the CLI speaks with unnamed.
    if (!(this.models.length === 1 && this.models[0]?.handle === speech.model) && speech.model !== '') command.push(`-m ${speech.model}`)
    if (speech.voice !== '' && speech.voice !== SayForm.defaultVoice) command.push(`--voice ${speech.voice}`)
    if ((speech.speed ?? 1) !== 1) command.push(`--speed ${SayForm.number(speech.speed ?? 1)}`)
    command.push(`-o ${tilde(speech.output)}`)
    return {
      shown: true,
      file: speech.name,
      meta: `${speech.voice} · ${speedText(speech.speed ?? 1)} · ${secs(total)} s · ${when(speech.createdAt, this.clock.now)}`,
      actions: [
        { action: 'settings', label: 'Use these settings', kind: 'btn' },
        { action: 'compositions', label: 'Use this voice for compositions', kind: 'btn-g' },
        { action: 'finder', label: 'Show in Finder', kind: 'btn-g' },
      ],
      bars,
      player: this.playback.face(key, total, { width: 1, height: 1 }),
      format: `16-bit mono WAV · ${speech.sampleRate === undefined ? 'its rate not recorded' : `${secs(speech.sampleRate / 1000)} kHz`} · ${secs(total)} s`,
      sentences: speech.sentences.map((sentence, index) => ({ n: String(index + 1), text: sentence.text, starts: `${sentence.offset.toFixed(2)} s`, lasts: `${sentence.seconds.toFixed(2)} s`, lit: index === lit })),
      measured: `Each sentence’s seconds are counted from the samples the model wrote. The file’s ${secs(total)} s are measured from its own WAV header.`,
      record: baseName(SpeechRecord.sidecarOf(speech)),
      facts: [
        { label: 'Model', value: speech.model === '' ? 'not recorded' : slash < 0 ? speech.model : `${speech.model.slice(0, slash)} · ${speech.model.slice(slash + 1)}`, mono: true },
        { label: 'Voice', value: `${speech.voice}${speech.language === undefined ? '' : ` · ${speech.language}`}`, mono: true },
        { label: 'Speed', value: speedText(speech.speed ?? 1), mono: false },
        { label: 'Device', value: speech.device.device ?? 'not recorded', mono: true },
        { label: 'Versions', value: [MediaEngine.name(speech.engine), ...ran].join(' · '), mono: true },
        { label: 'Segments', value: speech.segments === 1 ? '1: a speech is one segment, its sentences timed inside it' : `${speech.segments}: each measured from its own WAV, its sentences timed inside it`, mono: false },
        { label: 'Took', value: speech.runSeconds === undefined ? 'not recorded' : `${secs(speech.runSeconds)} s`, mono: false },
      ],
      command: command.join(' '),
    }
  }
}
