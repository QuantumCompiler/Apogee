import type { SuiteFootprint } from '../../../business/reads/SuiteFootprint'
import type { RunSettings } from '../../../data/protocol/DriverLine'

/**
 * A slash command, as the composer's `/` menu lists it: the old prototype's
 * list (`commands()`, `main.html:3780` at `b375fac9`), the CLI's slash
 * commands in its words. Machine mode carries none of them but `/play`, so
 * each the window acts on maps onto a line or a door that exists (the Mac
 * app's `SlashCommand`), and the rest are said as the old prototype says
 * them.
 */
export interface SlashCommand {
  readonly name: string
  /** What it takes, as the menu shows it: `[backend]`, `<path>`; empty for nothing. */
  readonly args: string
  readonly summary: string
}

const executeCommands: readonly SlashCommand[] = [
  { name: 'play', args: '<symphony> [input]', summary: 'Play a symphony on the input: its stages on the suite’s members, its output this session’s answer' },
  { name: 'symphonies', args: '', summary: 'List the symphonies /play can play' },
]

const chatCommands: readonly SlashCommand[] = [
  { name: 'model', args: '[backend]', summary: 'Show the backend answering, or switch to another' },
  { name: 'models', args: '', summary: 'List the configured backends' },
  { name: 'suite', args: '[name|off]', summary: 'Show the suite and what its members hold, or switch' },
  { name: 'think', args: '[on|off|auto]', summary: 'Show or set whether the model thinks first' },
  { name: 'compact', args: '', summary: 'Summarize the conversation so far, to free context' },
  { name: 'title', args: '<name>', summary: 'Rename this session' },
  { name: 'attach', args: '<path>', summary: 'Attach a file, folder or glob' },
  { name: 'attachments', args: '', summary: 'List what is attached' },
  { name: 'detach', args: '<name>', summary: 'Detach an attachment' },
  { name: 'recall', args: '[on|off]', summary: 'Show or set whether turns recall earlier sessions' },
  { name: 'private', args: '', summary: 'Never summarize this session for recall' },
  { name: 'allow', args: '[tool|website]', summary: 'Allow a tool or website for this session' },
  { name: 'deny', args: '<tool|website>', summary: 'Refuse a tool or website for this session, without asking' },
  { name: 'revoke', args: '<tool|website>', summary: 'Forget this session’s answer for a tool or website' },
  { name: 'permissions', args: '', summary: 'List each tool’s answer and where it comes from' },
  { name: 'system', args: '<text>', summary: 'Replace the system prompt' },
  { name: 'temperature', args: '<number>', summary: 'Set the sampling temperature' },
  { name: 'max-tokens', args: '<number>', summary: 'Cap each answer’s length in tokens' },
  { name: 'retriever', args: '[mode]', summary: 'How documents are searched: auto, lexical, vector or hybrid' },
  { name: 'rerank', args: '[backend]', summary: 'The model that reranks search results, or off, or auto' },
  { name: 'capture', args: '[status|link]', summary: 'Save this conversation as a knowledge record' },
  { name: 'help', args: '', summary: 'List these commands' },
]

export const SlashCommand = {
  /** Every command, in the order the menu shows them: an `execute` session's two first. */
  all(execute: boolean): readonly SlashCommand[] {
    return execute ? [...executeCommands, ...chatCommands] : chatCommands
  },

  /** The commands the draft's `/` names; `undefined` when the draft is not a command being typed (no `/` first, or a space typed). */
  matching(draft: string, execute: boolean): readonly SlashCommand[] | undefined {
    if (!draft.startsWith('/') || draft.includes(' ')) return undefined
    const typed = draft.slice(1).toLowerCase()
    return SlashCommand.all(execute).filter((command) => command.name.startsWith(typed))
  },
}

/** How a private session is kept from being written: the Mac app's `PrivateMode`. */
export type PrivateMode = 'nothingWritten' | 'deletedOnClose'

export const PrivateMode = {
  /** What a private session's state says of what is kept. */
  words(mode: PrivateMode): string {
    return mode === 'nothingWritten'
      ? 'Nothing of this session is written: no transcript, no recall in later sessions.'
      : 'Never recalled in later sessions, and deleted when you close it. This apogee writes it until then.'
  },
}

/**
 * The context meter: the last turn's input tokens against the model's
 * window -- the app's arithmetic over what the CLI reports, unknown when
 * either is missing, never guessed. The Mac app's `ContextMeter`.
 */
export interface ContextMeter {
  readonly used: number | undefined
  readonly window: number | undefined
  /** 0 to 100, or `undefined` when it cannot be known. */
  readonly percent: number | undefined
  /** At 80% and above the design warns. */
  readonly warns: boolean
  /** What the meter says in words. */
  readonly text: string
}

export const ContextMeter = {
  of(used: number | undefined, window: number | undefined): ContextMeter {
    const percent = used === undefined || window === undefined || window <= 0 ? undefined : Math.min(100, Math.round((used / window) * 100))
    const text =
      percent === undefined || used === undefined || window === undefined
        ? window === undefined
          ? 'unknown'
          : 'not reported'
        : `${percent}% · ${used.toLocaleString('en-US')} of ${window.toLocaleString('en-US')} tokens`
    return { used, window, percent, warns: (percent ?? 0) >= 80, text }
  },

  /** A count as the old meter writes it: `820`, `2.1K`, `32.8K`. */
  short(count: number): string {
    return count >= 1000 ? `${Math.round(count / 100) / 10}K` : String(count)
  },
}

/** A suite the chooser offers: its name, and the CLI's word on its fit here (`models status --suite <name>`). The Mac app's `SuiteChoice`. */
export interface SuiteChoice {
  readonly name: string
  /** The CLI's footprint of it; `undefined` where the read stated none. */
  readonly footprint: SuiteFootprint | undefined
  /** It is `models.default_suite`: a new chat with no `--suite` runs under it. */
  readonly isDefault: boolean
}

export const SuiteChoice = {
  /** The CLI prices it over this machine's memory: it runs only forced. */
  overBudget(choice: SuiteChoice): boolean {
    return choice.footprint?.verdict === 'doesNotFit'
  },
}

/** The word `--suite` takes for none. */
export const noSuite = 'off'

/** What an attachment is, as its chip's glyph draws it: the old prototype's six kinds (`main.html:731-736` at `b375fac9`). */
export type AttachmentKind = 'folder' | 'file' | 'pdf' | 'image' | 'audio' | 'video'

/** The CLI's own media extensions (`media.cpp`'s `medium_of`), each list as it writes it. */
const media: ReadonlyArray<readonly [AttachmentKind, readonly string[]]> = [
  ['image', ['.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp', '.tif', '.tiff', '.heic', '.heif']],
  ['audio', ['.wav', '.mp3', '.m4a', '.aac', '.flac', '.ogg', '.oga', '.opus', '.aiff', '.aif', '.wma']],
  ['video', ['.mp4', '.mov', '.m4v', '.mkv', '.webm', '.avi', '.wmv', '.mpg', '.mpeg']],
]

export const AttachmentKind = {
  /** Read from the path: a trailing `/`, `.` or `..` a folder, `.pdf` a PDF, the CLI's image, audio and video extensions (lowercased, as it reads them), else a file. */
  of(path: string): AttachmentKind {
    if (path.endsWith('/') || path === '.' || path === '..') return 'folder'
    const name = path.split('/').at(-1) ?? path
    const dot = name.lastIndexOf('.')
    const extension = dot > 0 ? name.slice(dot).toLowerCase() : ''
    if (extension === '.pdf') return 'pdf'
    return media.find(([, extensions]) => extensions.includes(extension))?.[0] ?? 'file'
  },
}

/**
 * A line held until the session can take it: made while a turn ran or a card
 * stood, or while a stop handed the session to another child (GUI 6a), sent
 * once the turn ends or the next child speaks -- kept through a death for
 * Start again. The Mac app's `HeldLine`.
 */
export type HeldLine =
  | { readonly case: 'attach'; readonly path: string }
  | { readonly case: 'detach'; readonly path: string }
  | { readonly case: 'set'; readonly settings: RunSettings }
  | { readonly case: 'compact' }
  /** A move to another suite, or none, made mid-turn: no line carries it, so the session restarts under it once the turn ends (GUI 4i). */
  | { readonly case: 'suite'; readonly suite: string; readonly force: boolean }

export const HeldLine = {
  /** What it will do, in a few words: `attach report.pdf`, `temperature 0.9`, `compact` (the Mac app's `HeldLine.words`). */
  words(line: HeldLine): string {
    switch (line.case) {
      case 'attach':
        return `attach ${line.path}`
      case 'detach':
        return `detach ${line.path}`
      case 'set':
        return RunSettingsWords.of(line.settings)
      case 'compact':
        return 'compact'
      case 'suite':
        return line.suite === noSuite ? 'leave the suite' : `switch to suite ${line.suite}`
    }
  },
}

/** A settings change in a few words, as the Mac app's `SessionViewModel.words` says one: `model quiet`, `temperature 0.4`, `thinking on`. */
export const RunSettingsWords = {
  of(settings: RunSettings): string {
    const parts: string[] = []
    if (settings.model !== undefined) parts.push(`model ${settings.model}`)
    if (settings.think !== undefined) parts.push(`thinking ${settings.think}`)
    if (settings.effort !== undefined) parts.push(`effort ${settings.effort}`)
    if (settings.system !== undefined) parts.push('a new system prompt')
    if (settings.temperature !== undefined) parts.push(`temperature ${settings.temperature}`)
    return parts.join(', ')
  },
}
