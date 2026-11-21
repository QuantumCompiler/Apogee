import type { InstallCardFace } from '../../styles/Media/Install'
import type { PhaseFace } from '../../styles/Media/Phase'
import type { MediaPlayerFace } from '../../styles/Media/Playback'

/** The Video place's two tabs: its clips, and its compositions (1l's, drawn in the app root's slot). */
export type VideoTab = 'clips' | 'compositions'

/** A clip page's actions, by what each does. */
export type ClipAction = 'final' | 'again' | 'reroll' | 'extend' | 'finder'

/** A button's look: the old prototype's three. */
export type ButtonKind = 'btn' | 'btn-p' | 'btn-g'

/** One row of the clips list: the run going now, or a clip. */
export interface ClipRowFace {
  /** The clip's path, or `run` for the run. */
  readonly id: string
  readonly file: string
  readonly prompt: string
  readonly tag: string
  readonly tagColor: string
  readonly meta: string
  readonly metaColor: string
  readonly running: boolean
  readonly current: boolean
}

/** The run going now, or the last one until dismissed: the old `runView`. */
export interface RunFace {
  readonly file: string
  readonly prompt: string
  readonly live: boolean
  /** Ended without its clip: stopped by the person, or failed in the CLI's words. */
  readonly ended: boolean
  readonly status: string
  readonly statusColor: string
  readonly phases: readonly PhaseFace[]
  /** What the run says, one line a line: the CLI's notices and its narration. */
  readonly log: string
  readonly foot: string
}

/** A fact of a clip's record. */
export interface FactFace {
  readonly label: string
  readonly value: string
  readonly mono: boolean
}

/** A clip's page: the old `clipView`. */
export interface ClipFace {
  readonly id: string
  readonly file: string
  readonly preset: string
  readonly presetColor: string
  readonly modeText: string
  readonly prompt: string
  readonly player: MediaPlayerFace
  readonly actions: ReadonlyArray<{ readonly action: ClipAction; readonly label: string; readonly kind: ButtonKind; readonly title: string }>
  /** A record made on another platform: said, the same seed may not make the same clip here. */
  readonly note: string
  readonly facts: readonly FactFace[]
  readonly loras: ReadonlyArray<{ readonly path: string; readonly scale: string; readonly sha: string; readonly adapter: string }>
  /** The clip itself first, then each it came from; `go` the path a link opens, empty on the clip itself. */
  readonly chain: ReadonlyArray<{ readonly label: string; readonly how: string; readonly go: string }>
  readonly files: ReadonlyArray<{ readonly path: string; readonly size: string }>
  readonly command: string
}

/** A choice of a segmented control. */
export interface SegmentFace {
  readonly value: string
  readonly label: string
}

/** The generate form: the old `fv`. */
export interface FormFace {
  readonly heading: string
  readonly notNew: boolean
  /** What the CLI will say of where the run came from, a line a line; empty for a new clip. */
  readonly told: string
  readonly prompt: string
  readonly continuing: boolean
  readonly continues: string
  readonly image: string
  readonly canAddImage: boolean
  readonly pipeline: string
  readonly pipelines: readonly SegmentFace[]
  readonly lockFrame: boolean
  readonly preset: string
  readonly presets: readonly SegmentFace[]
  readonly presetNote: string
  readonly dur: string
  readonly fps: string
  readonly steps: string
  readonly size: string
  readonly sizes: readonly SegmentFace[]
  readonly framesText: string
  readonly seedMode: string
  readonly seedModes: readonly SegmentFace[]
  readonly seedGiven: boolean
  readonly seed: string
  readonly seedNote: string
  readonly loraCount: string
  readonly loras: ReadonlyArray<{ readonly path: string; readonly name: string; readonly state: string; readonly scale: string }>
  readonly loraFull: boolean
  readonly out: string
  readonly errors: readonly string[]
  readonly runsOn: string
  readonly expect: string
  readonly memory: string
  readonly command: string
  readonly canGenerate: boolean
  readonly goNote: string
}

/** Everything the Video place draws. */
export interface VideoFace {
  readonly tab: VideoTab
  readonly tabs: ReadonlyArray<{ readonly tab: VideoTab; readonly label: string; readonly count: string; readonly current: boolean }>
  readonly pipelineLine: string
  readonly newLabel: string
  readonly canNew: boolean
  /** The install the person said yes to, as its card draws it, `shown` while there is one. */
  readonly install: InstallCardFace & { readonly shown: boolean }
  readonly list: readonly ClipRowFace[]
  /** What the library passed over, and why. */
  readonly skipped: readonly string[]
  readonly empty: string
  /** The run drawn in place of a clip's page: selected, or no clip to show. */
  readonly showRun: boolean
  /** The run going now, or the last until dismissed, `shown` while there is one. */
  readonly run: RunFace & { readonly shown: boolean }
  /** The clip shown, `shown` while there is one. */
  readonly clip: ClipFace & { readonly shown: boolean }
  readonly form: FormFace
  /** The consent the CLI's refusal asks for, waiting on a yes, `shown` while it waits. */
  readonly consent: { readonly shown: boolean; readonly title: string; readonly said: string; readonly body: string; readonly command: string; readonly confirm: string; readonly cancel: string }
}
