import { JobProgress } from '../../../business/jobs/JobProgress'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { Symphony, SymphonyPlay } from '../../../business/reads/Symphonies'
import { ReadDocument } from '../../../data/protocol/ReadDocument'
import type { FolderWriting } from '../../../infrastructure/process/StandInFolders'
import { afterTicks, DefinitionEditing, DefinitionEditor, saveTicks } from './DefinitionEditor'
import type { LibraryDoors, Ticks } from './DefinitionEditor'
import { LibraryListing, splitPath } from './LibraryListing'

/** The image types `symphonies play --image` names; the binary judges the file itself. */
export const imageTypes = ['png', 'jpg', 'gif', 'webp', 'bmp'] as const

/**
 * The Library's symphonies (design 1n): the Mac app's `SymphoniesViewModel`
 * (`LibraryViewModels.swift:383-600`) -- the catalog, a play supervised with
 * its stages narrated, given its image where its input takes one, and the
 * person's own spec file edited in place and checked by the binary, closed
 * asked first when typing or the original would be lost (GUI 6f). Each row
 * picks, as the old prototype's symphony rows do. Plain TypeScript.
 */
export class SymphoniesViewModel extends DefinitionEditing {
  symphonies: readonly Symphony[] = []
  listing: LibraryListing = LibraryListing.notRead
  /** The symphony shown: the one picked, else the list's first, as the old page shows `critique-revise`. */
  private picked: string | undefined = undefined
  /** Each symphony's input as typed; the old page's own words to start (`sampleInputs`, handed in). */
  private inputs: Record<string, string> = {}
  /** The image a play gives a symphony whose input takes one (`--image`): the file's path, as chosen; cleared when another symphony is chosen (GUI 6f). */
  image: string | undefined = undefined
  /** The image Choose an image… offers: a browser picks no file for the CLI, so the old prototype's sample path, handed in. */
  sampleImage = ''
  /** A play running now and its progress, and the last play's document: each lands on its own symphony's page and no other (GUI 4h). */
  playing: JobProgress | undefined = undefined
  playingName: string | undefined = undefined
  lastPlay: SymphonyPlay | undefined = undefined
  playedName: string | undefined = undefined
  private job: JobRunning | undefined = undefined
  private binary: string | undefined = undefined

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
    private readonly folders: FolderWriting,
    private readonly clock: Ticks,
    doors: LibraryDoors,
  ) {
    super(doors)
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    if (binary === undefined) this.listing = LibraryListing.missing
    this.changed()
  }

  /** `symphonies list`; a failure is said where the list is, the rows read before kept. */
  async refresh(): Promise<void> {
    const binary = this.binary
    if (binary === undefined) return
    const outcome = await this.runner.read(binary, ['symphonies', 'list'])
    const document = ReadOutcome.documentOf(outcome)
    if (document) this.symphonies = Symphony.list(document)
    this.listing = LibraryListing.of(outcome)
    this.changed()
  }

  /** What the list says under its rows. */
  get emptyText(): string | undefined {
    return LibraryListing.text(this.listing, 'symphonies')
  }

  get selected(): string | undefined {
    return this.picked ?? this.symphonies[0]?.name
  }

  get current(): Symphony | undefined {
    const name = this.selected
    return this.symphonies.find((symphony) => symphony.name === name)
  }

  /**
   * Chooses a symphony; another one's image and last play go, as the old
   * prototype's pick drops them (`main.html:6611` at `b375fac9`). A running
   * play goes on, and lands on its own page.
   */
  select(name: string): void {
    if (this.selected !== name) {
      this.image = undefined
      this.lastPlay = undefined
      this.playedName = undefined
    }
    this.picked = name
    this.changed()
  }

  /** The inputs the page starts with, by symphony. */
  setInputs(inputs: Readonly<Record<string, string>>): void {
    this.inputs = { ...inputs, ...this.inputs }
    this.changed()
  }

  /** The shown symphony's input. */
  get input(): string {
    return this.inputs[this.selected ?? ''] ?? ''
  }

  setInput(text: string): void {
    const name = this.selected
    if (name === undefined) return
    this.inputs = { ...this.inputs, [name]: text }
    this.changed()
  }

  /** The chosen image's file name, as the page shows it. */
  get imageName(): string | undefined {
    return this.image === undefined ? undefined : splitPath(this.image).name
  }

  /** Choose an image… (and Choose another…): the old prototype's sample. */
  chooseImage(): void {
    if (this.current?.takesImage !== true) return
    this.image = this.sampleImage === '' ? undefined : this.sampleImage
    this.changed()
  }

  removeImage(): void {
    this.image = undefined
    this.changed()
  }

  /** Whether the symphony shown takes an image and none is chosen yet: Play waits, and the page says so. Only the symphony's own document decides -- `input.image` (GUI 6f). */
  get needsImage(): boolean {
    return this.current?.takesImage === true && this.image === undefined
  }

  /** Whether Play can start: a symphony, no play running, its input, and its image where it takes one. */
  get canPlay(): boolean {
    if (this.binary === undefined || this.current === undefined || this.job !== undefined || this.needsImage) return false
    return this.input.trim() !== ''
  }

  get isPlaying(): boolean {
    return this.job !== undefined
  }

  /** The last play's document, on the page of the symphony it played -- never another's. */
  get shownPlay(): SymphonyPlay | undefined {
    return this.playedName !== undefined && this.playedName === this.selected ? this.lastPlay : undefined
  }

  /** The running play's progress, on its own symphony's page alone. */
  get shownProgress(): JobProgress | undefined {
    return this.playingName !== undefined && this.playingName === this.selected ? this.playing : undefined
  }

  /**
   * `symphonies play <name> --input <text> [--image <path>] --output-format
   * json`, supervised: the stage lines as it goes, the stages' answers at
   * the end. A play nobody recorded ends in the stand-in's words, said.
   */
  play(): void {
    const binary = this.binary
    const symphony = this.current
    if (!this.canPlay || binary === undefined || !symphony) return
    const args = ['symphonies', 'play', symphony.name, '--input', this.input.trim()]
    if (symphony.takesImage && this.image !== undefined) args.push('--image', this.image)
    args.push('--output-format', 'json')
    this.lastPlay = undefined
    this.playedName = undefined
    const progress = new JobProgress()
    this.playing = progress
    this.playingName = symphony.name
    const job = this.starter.startJob(binary, args, false)
    this.job = job
    this.changed()
    void (async () => {
      const stdout: string[] = []
      for await (const update of job.updates) {
        if (update.case === 'said') stdout.push(update.line)
        progress.apply(update)
        this.changed()
      }
      this.job = undefined
      this.playing = undefined
      this.playingName = undefined
      const document = ReadDocument.from(stdout.join('\n'))
      const played = document ? SymphonyPlay.read(document) : undefined
      if (played) {
        this.lastPlay = played
        this.playedName = symphony.name
      } else if (progress.status.case === 'failed') {
        this.doors.say?.(`${symphony.name}: ${progress.status.message}`)
      }
      this.changed()
    })()
  }

  async stop(): Promise<void> {
    await this.job?.stop(10_000)
  }

  /** Opens the person's own spec file in the editor; a shipped starter or a config entry is edited at a prompt (`symphonies edit`). */
  edit(): void {
    const symphony = this.current
    if (!symphony || !Symphony.isOwnFile(symphony) || symphony.path === undefined) return
    const { folder, name } = splitPath(symphony.path)
    const text = this.folders.files(folder).find((entry) => entry.name === name)?.text
    if (text === undefined) return
    this.open(new DefinitionEditor(symphony.name, symphony.path, text))
  }

  /**
   * Writes the spec file, then asks the binary once the save's ticks have
   * passed: `symphonies show` refuses a file it cannot read, in its words,
   * and lists the problems of one it can. The editor is read-only until the
   * binary has answered, and its word lands on the editor as it stands then:
   * one closed meanwhile stays closed.
   */
  async save(): Promise<void> {
    const editor = this.editor
    const binary = this.binary
    const name = editor?.title
    if (!editor || editor.saving || binary === undefined || name === undefined) return
    const saved = editor.text
    const { folder, name: file } = splitPath(editor.path)
    this.folders.write(folder, { name: file, text: saved })
    editor.wrote(saved)
    editor.saving = true
    this.changed()
    await afterTicks(this.clock, saveTicks)
    const outcome = await this.runner.read(binary, ['symphonies', 'show', name])
    if (this.editor?.path === editor.path) {
      const document = ReadOutcome.documentOf(outcome)
      const symphony = document ? Symphony.read(document.value) : undefined
      if (symphony) this.editor.endSave(saved, symphony.problems.length === 0 ? 'Saved, and apogee reads it.' : symphony.problems.join('\n'), symphony.problems.length > 0)
      else this.editor.endSave(saved, ReadOutcome.failure(outcome) ?? 'apogee could not read it', true)
      this.changed()
    }
    await this.refresh()
  }

  /** Put it back as it was is offered once apogee refused a save (2e). */
  get offersRestore(): boolean {
    return this.editor?.refused === true
  }
}
