import type { PhaseFace } from '../Media/Phase'

/**
 * One phase of a run (design 1k: the old prototype's `phaseRow`, promoted to
 * `styles` for a clip's run and a composition's render): its mark -- done,
 * running, not started or stopped, each said to a screen reader -- its label
 * with its bar where it has one, and its detail.
 */
export function PhaseRow({ phase }: { readonly phase: PhaseFace }) {
  const { status } = phase
  const barColor = status === 'stopped' ? 'var(--status-warning)' : status === 'done' ? 'var(--text-secondary)' : 'var(--status-accent)'
  return (
    <div style={{ display: 'grid', gridTemplateColumns: '18px minmax(0, 1fr) auto', gap: 10, alignItems: 'center', padding: '8px 0' }}>
      <span style={{ display: 'grid', placeItems: 'center', width: 16, height: 16 }}>
        {status === 'done' ? (
          <svg className="i" width="14" height="14" viewBox="0 0 16 16" aria-label="done" style={{ color: 'var(--status-accent)' }}>
            <path d="M3.5 8.5l3 3 6-7" />
          </svg>
        ) : status === 'running' ? (
          <span className="pulse" aria-label="running" style={{ width: 8, height: 8, borderRadius: '50%', background: 'var(--status-accent)' }} />
        ) : status === 'pending' ? (
          <span aria-label="not started" style={{ width: 8, height: 8, boxSizing: 'border-box', borderRadius: '50%', border: '1.5px solid var(--text-tertiary)' }} />
        ) : (
          <span aria-label="stopped" style={{ width: 8, height: 8, borderRadius: 2, background: 'var(--status-warning)' }} />
        )}
      </span>
      <div style={{ minWidth: 0 }}>
        <div style={{ fontSize: 'var(--t-body)', color: status === 'pending' ? 'var(--text-tertiary)' : 'var(--text-primary)' }}>{phase.label}</div>
        {phase.bar === undefined ? null : (
          <div style={{ height: 4, marginTop: 6, borderRadius: 2, background: 'var(--line-border)', overflow: 'hidden' }}>
            <div style={{ width: `${phase.bar}%`, height: '100%', background: barColor }} />
          </div>
        )}
      </div>
      <span className="mono" style={{ fontSize: 'var(--t-meta)', color: status === 'stopped' ? 'var(--status-warning)' : 'var(--text-tertiary)', textAlign: 'right' }}>
        {phase.detail}
      </span>
    </div>
  )
}
