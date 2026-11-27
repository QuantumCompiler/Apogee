/**
 * What Review's views draw (design 1n), as words: every absence said
 * (`shown: false`, `''`), so the per-preset walk reads no `undefined`.
 */

/** The place's three tabs: the Mac app's `ReviewViewModel.Tab`. */
export type ReviewTab = 'runs' | 'targets' | 'skills'

export interface ReviewTabFace {
  readonly tab: ReviewTab
  readonly label: string
  readonly current: boolean
}

/** One finding, as its card draws it. */
export interface FindingFace {
  readonly severity: string
  readonly severityColor: string
  readonly kind: string
  /** `file:line`, or the file alone. */
  readonly at: string
  /** `high confidence · security`. */
  readonly by: string
  readonly claim: string
  readonly fix: string
}

/** The run on screen: its document's title, where and how, why nothing was posted, its findings and its counts. */
export interface RunFace {
  readonly shown: boolean
  readonly title: string
  readonly where: string
  readonly lenses: string
  readonly posted: string
  readonly canPost: boolean
  readonly findings: readonly FindingFace[]
  readonly counts: string
}

/** The Reviews tab: the form, its command, what it says with no apogee, and the run. */
export interface RunsFace {
  readonly form: { readonly ref: string; readonly lenses: string; readonly model: string; readonly command: string }
  readonly missing: string
  readonly run: RunFace
}

/** A target's row. */
export interface TargetRowFace {
  readonly name: string
  readonly facts: string
  readonly post: string
  readonly postColor: string
}

/** The last sweep, as its document says it. */
export interface SweepFace {
  readonly shown: boolean
  readonly summary: string
  readonly targets: ReadonlyArray<{
    readonly heading: string
    readonly error: string
    readonly projects: ReadonlyArray<{ readonly project: string; readonly watermark: string; readonly error: string; readonly lines: readonly string[] }>
  }>
  readonly kept: string
}

export interface TargetsFace {
  readonly note: string
  readonly rows: readonly TargetRowFace[]
  readonly sweep: SweepFace
}

/** A lens's row: its name and what it applies to. */
export interface SkillRowFace {
  readonly name: string
  readonly meta: string
  readonly current: boolean
}

export interface SkillsFace {
  readonly note: string
  readonly rows: readonly SkillRowFace[]
  /** What the page says with no lens to show. */
  readonly empty: string
  readonly skill: { readonly shown: boolean; readonly name: string; readonly description: string; readonly applies: string; readonly prompt: string; readonly command: string }
}

export interface ReviewFace {
  readonly tabs: readonly ReviewTabFace[]
  /** Add target… and Sweep…, on the targets tab, off with no apogee. */
  readonly targetButtons: { readonly shown: boolean; readonly enabled: boolean }
  readonly runs: RunsFace
  readonly targets: TargetsFace
  readonly skills: SkillsFace
}

/** The add-target sheet's content. */
export interface TargetSheetFace {
  readonly shown: boolean
  readonly name: string
  readonly watch: 'repo' | 'owner'
  readonly repo: string
  readonly owner: string
  readonly forge: string
  readonly host: string
  readonly lensesLabel: string
  readonly lensOpen: boolean
  readonly lenses: ReadonlyArray<{ readonly name: string; readonly on: boolean }>
  readonly post: boolean
  readonly drafts: boolean
  readonly model: string
  readonly command: string
  readonly canAdd: boolean
}

/** The post sheet's words: the confirm host's title, body and the command it would run. */
export interface PostWords {
  readonly title: string
  readonly body: string
  readonly command: string
}
