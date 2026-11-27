import type { MediaPlayerFace } from '../../styles/Media/Playback'
import type { PhaseFace } from '../../styles/Media/Phase'

/** One choice of a select: its value and its words. */
export interface ComposeChoice {
  readonly value: string
  readonly label: string
}

/** A line of words and the color it is drawn in. */
export interface ColoredLine {
  readonly text: string
  readonly color: string
}

/** One composition in the list: the old `comp.list` row. */
export interface ProjectRowFace {
  readonly output: string
  readonly title: string
  readonly file: string
  /** `EDIT 1`, for an edit; empty for an original. */
  readonly tag: string
  readonly status: string
  readonly statusColor: string
  readonly rendering: boolean
  readonly current: boolean
}

/** The draft's row above the list: the old `comp.hasDraft`. */
export interface DraftRowFace {
  readonly shown: boolean
  readonly current: boolean
  readonly meta: string
}

/** One planned scene, as the new composition's plan draws it. */
export interface PlannedSceneFace {
  readonly n: string
  readonly narration: string
  readonly shows: string
  readonly secs: string
}

/** A label, its words and their color: the plan's PLAN, SPEND, CAPTIONS and MUSIC lines. */
export interface SpendLineFace {
  readonly label: string
  readonly value: string
  readonly color: string
}

/** The new composition: the old `comp.nw` -- the brief and its plan in the middle, its settings in the pane. */
export interface NewCompositionFace {
  readonly brief: string
  readonly count: string
  readonly countColor: string
  readonly planning: boolean
  readonly planningText: string
  readonly cantPlan: boolean
  readonly planLabel: string
  readonly planNote: string
  readonly planned: boolean
  readonly title: string
  readonly lengthText: string
  readonly scenes: readonly PlannedSceneFace[]
  readonly spend: readonly SpendLineFace[]
  readonly out: string
  readonly planner: string
  readonly planners: readonly ComposeChoice[]
  readonly plannerNote: string
  readonly plannerColor: string
  readonly pipeline: string
  readonly pipelines: readonly ComposeChoice[]
  readonly voice: string
  readonly voices: readonly ComposeChoice[]
  readonly hasVoiceModel: boolean
  readonly voiceModel: string
  readonly voiceModels: readonly ComposeChoice[]
  /** The music bed's file name; empty for none. */
  readonly music: string
  /** The assets folder and what it holds; empty for none. */
  readonly assets: string
  readonly assetsMeta: string
  readonly captions: boolean
  readonly captionsLine: string
  readonly captionsRemedy: string
  readonly command: string
}

/** One block of the timeline's picture row. */
export interface PictureBlockFace {
  readonly n: string
  readonly at: string
  readonly len: string
  readonly left: number
  readonly width: number
  readonly current: boolean
  readonly made: boolean
}

/** A bar on the timeline: a narration or a caption cue, by its share of the whole. */
export interface TimelineBarFace {
  readonly left: number
  readonly width: number
  readonly made: boolean
}

/** The timeline under the player: the old `comp.p.tl`. */
export interface TimelineFace {
  readonly pictures: readonly PictureBlockFace[]
  readonly narration: readonly TimelineBarFace[]
  readonly cues: readonly TimelineBarFace[]
  readonly bed: boolean
  readonly head: number
  readonly legend: string
  readonly end: string
  readonly note: string
}

/** One scene of a project's page. */
export interface ProjectSceneFace {
  readonly n: string
  readonly narration: string
  readonly shows: string
  readonly voiceText: string
  readonly voiceColor: string
  readonly clipText: string
  readonly clipColor: string
  readonly seedText: string
  readonly secs: string
  readonly playing: boolean
  readonly canRedo: boolean
  readonly redo: boolean
}

/** One row of an edit's CHANGES, or of its preview's diff. */
export interface ChangeRowFace {
  readonly n: string
  readonly text: string
  readonly color: string
  readonly narration: string
}

/** A fact of the SETTINGS list. */
export interface ProjectFactFace {
  readonly label: string
  readonly value: string
  readonly mono: boolean
  readonly color: string
}

/** One link of the CHAIN: the project and where it came from. */
export interface ChainLinkFace {
  readonly output: string
  readonly label: string
  readonly how: string
  readonly link: boolean
  readonly first: boolean
  readonly last: boolean
}

/** A project's page: the old `comp.p`. */
export interface ProjectPageFace {
  readonly shown: boolean
  readonly title: string
  readonly tag: string
  readonly meta: string
  readonly brief: string
  readonly status: string
  readonly statusColor: string
  /** Its render runs here: Stop. */
  readonly rendering: boolean
  /** Stopped, or failed: Resume. */
  readonly stopped: boolean
  readonly showProgress: boolean
  readonly phases: readonly PhaseFace[]
  readonly stopNote: string
  readonly isEdit: boolean
  readonly changes: readonly ChangeRowFace[]
  readonly differs: string
  readonly showPlayer: boolean
  readonly player: MediaPlayerFace
  readonly timeline: TimelineFace
  readonly scenes: readonly ProjectSceneFace[]
  readonly folder: string
  readonly files: readonly { readonly path: string; readonly what: string }[]
  readonly facts: readonly ProjectFactFace[]
  readonly chain: readonly ChainLinkFace[]
  readonly file: string
}

/** Make changes: the old `comp.ed`. */
export interface EditPaneFace {
  readonly locked: boolean
  readonly lockedText: string
  readonly text: string
  readonly textNote: string
  readonly redo: ColoredLine
  readonly voice: string
  readonly voices: readonly ComposeChoice[]
  readonly pipeline: string
  readonly pipelines: readonly ComposeChoice[]
  readonly music: string
  readonly musicChoices: readonly ComposeChoice[]
  readonly captions: boolean
  readonly captionsLabel: string
  readonly previewing: boolean
  readonly previewingText: string
  readonly cantPreview: boolean
  readonly previewNote: string
  readonly failure: string
  readonly shown: boolean
  readonly head: string
  readonly diff: readonly ChangeRowFace[]
  readonly notes: readonly string[]
  readonly spend: string
  readonly replans: ColoredLine
  readonly renderTitle: string
  readonly out: string
  readonly command: string
}

/** Everything the compositions tab draws: the old `comp`. */
export interface ComposeFace {
  readonly draft: DraftRowFace
  readonly isNew: boolean
  readonly list: readonly ProjectRowFace[]
  readonly skipped: readonly string[]
  readonly paneLabel: string
  readonly fresh: NewCompositionFace
  readonly page: ProjectPageFace
  readonly edit: EditPaneFace
}
