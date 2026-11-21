import type { TrainingTab } from './TrainingViewModel'

/**
 * Everything the Training place's views draw (design 1j), as words: absence
 * said explicitly (empty, `shown: false`), so the per-preset walk reads it
 * whole and finds no `undefined`.
 */
export interface TrainingFace {
  readonly tab: TrainingTab
  readonly tabs: readonly { readonly tab: TrainingTab; readonly label: string; readonly current: boolean }[]
  readonly header: {
    readonly newRun: { readonly shown: boolean; readonly enabled: boolean }
    readonly cycle: { readonly shown: boolean; readonly canRun: boolean; readonly halted: boolean; readonly canHalt: boolean }
    readonly datasets: { readonly shown: boolean; readonly enabled: boolean; readonly canPull: boolean }
  }
  /** The act running now: the old bar's label, its line, Stop. */
  readonly bar: { readonly shown: boolean; readonly label: string; readonly line: string; readonly canStop: boolean }
  readonly runs: RunsFace
  readonly pipelines: PipelinesFace
  readonly cycle: CycleFace
  readonly regimes: RegimesFace
  readonly datasets: DatasetsFace
  readonly promoteSheet: PromoteSheetFace
  readonly pullSheet: { readonly ref: string; readonly command: string; readonly canPull: boolean }
}

export interface Fact {
  readonly label: string
  readonly value: string
}

export interface RunsFace {
  readonly rows: readonly { readonly id: string; readonly status: string; readonly meta: string; readonly failed: boolean; readonly current: boolean }[]
  readonly missing: string
  readonly failure: string
  readonly versionsShown: boolean
  readonly versionsFailure: string
  readonly versions: readonly { readonly backend: string; readonly canRoll: boolean; readonly lines: readonly string[] }[]
  readonly noneChosen: string
  readonly detail: { readonly shown: boolean; readonly id: string; readonly gate: string; readonly failed: boolean; readonly facts: readonly Fact[] }
}

export interface StageRowFace {
  readonly index: string
  readonly name: string
  readonly line: string
  /** How its line is colored: passed in the accent, failed in the error color, pending tertiary. */
  readonly tone: 'passed' | 'failed' | 'pending' | 'active'
  readonly runLine: string
}

export interface PageFace {
  /** The run going here, a run's manifest, or none chosen. */
  readonly kind: 'live' | 'run' | 'none'
  readonly id: string
  readonly line: string
  readonly failed: boolean
  readonly when: string
  readonly stages: readonly StageRowFace[]
  readonly noStages: boolean
  readonly resume: { readonly shown: boolean; readonly line: string; readonly command: string; readonly enabled: boolean; readonly hold: string }
  readonly endLine: string
  readonly isCyclePass: boolean
  /** Why the chosen run's manifest could not be read, in the CLI's (or the stand-in's) words. */
  readonly failure: string
}

export interface PipelinesFace {
  readonly specs: readonly { readonly name: string; readonly meta: string; readonly command: string; readonly label: string; readonly canRun: boolean }[]
  readonly noSpecs: string
  readonly live: { readonly shown: boolean; readonly id: string; readonly current: boolean }
  readonly rows: readonly { readonly id: string; readonly line: string; readonly failed: boolean; readonly current: boolean }[]
  readonly noRuns: string
  readonly failure: string
  readonly page: PageFace
}

export interface CycleFace {
  readonly schedule: string
  readonly hold: string
  readonly lock: string
  readonly live: { readonly shown: boolean; readonly lines: readonly { readonly state: string; readonly text: string; readonly latest: boolean }[]; readonly starting: boolean; readonly stages: readonly StageRowFace[] }
  readonly error: string
  readonly errorHeld: string
  readonly failure: string
  readonly noHistory: string
  readonly hasHistory: boolean
  readonly facts: readonly Fact[]
  readonly passes: readonly { readonly at: string; readonly pipeline: string; readonly line: string; readonly failed: boolean; readonly passed: boolean; readonly reason: string; readonly note: string }[]
  /** The notice a pass run here ended with, its halt kept, while the loop stays halted. */
  readonly keptHalt: string
}

export interface RegimesFace {
  readonly rows: readonly { readonly name: string; readonly meta: string; readonly current: boolean }[]
  readonly failure: string
  readonly detail: { readonly shown: boolean; readonly name: string; readonly description: string; readonly kits: readonly string[] }
}

export interface DatasetsFace {
  readonly pulled: { readonly shown: boolean; readonly line: string; readonly path: string; readonly command: string; readonly canPrepare: boolean }
  readonly rows: readonly { readonly name: string; readonly meta: string; readonly current: boolean }[]
  readonly missing: string
  readonly failure: string
  readonly kits: readonly { readonly name: string; readonly meta: string }[]
  readonly kitsFailure: string
  readonly noneChosen: string
  readonly detail: { readonly shown: boolean; readonly name: string; readonly line: string; readonly path: string }
}

export interface PromoteSheetFace {
  readonly title: string
  /** The CLI's refusal the sheet was opened again with. */
  readonly error: string
  readonly verdict: { readonly text: string; readonly failed: boolean }
  readonly adapter: boolean
  readonly backend: string
  /** The entries an adapter can go onto: the backends the ledgers name. */
  readonly entries: readonly string[]
  readonly ledger: string
  readonly suite: string
  readonly judge: string
  readonly target: string
  readonly quantize: string
  readonly keepFused: boolean
  readonly trainer: string
  readonly force: boolean
  readonly command: string
  readonly canPromote: boolean
}
