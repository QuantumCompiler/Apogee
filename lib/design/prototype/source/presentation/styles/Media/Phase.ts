/** How a run's phase stands: the old prototype's `phaseRow` statuses. */
export type PhaseStatus = 'done' | 'running' | 'pending' | 'stopped'

/** One phase of a run, as `PhaseRow` draws it: its label, how it stands, its detail, and its bar where it has one (0 to 100). */
export interface PhaseFace {
  readonly label: string
  readonly status: PhaseStatus
  readonly detail: string
  readonly bar?: number | undefined
}

/** A phase's status by where a run is: before it pending, at it running (stopped once the run was), past it done. */
export function phaseStatus(phase: number, at: number, stopped: boolean): PhaseStatus {
  return at > phase ? 'done' : at === phase ? (stopped ? 'stopped' : 'running') : 'pending'
}
