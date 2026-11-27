import type { ReactNode } from 'react'

/**
 * The old prototype's list of definitions (`main.html` at `b375fac9`: the
 * Library's `Definitions` nav, `ui.isLibrary`, and Review's `Skills` nav,
 * `ui.isReview`): a 300 px column on the sidebar's surface, each row a
 * button with its name and a line under it, the current one drawn on the
 * selection. Promoted to `styles` by design 1n on its second user, Review's
 * skills beside the Library's definitions.
 */
export function PickColumn({ label, children }: { readonly label: string; readonly children: ReactNode }) {
  return (
    <nav aria-label={label} style={{ flex: '0 0 300px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', background: 'var(--surface-sidebar)', borderRight: '1px solid var(--line-border)' }}>
      {children}
    </nav>
  )
}

/**
 * One row: its name, and under it its line -- the Library's at the meta
 * size, a skill's in mono at the body size, cut to one line (`mono`).
 */
export function PickRow({ name, meta, current, pick, mono = false }: { readonly name: string; readonly meta: string; readonly current: boolean; readonly pick: () => void; readonly mono?: boolean }) {
  return (
    <button
      type="button"
      className="hv"
      onClick={pick}
      aria-current={current}
      style={{ width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '10px 12px', marginBottom: 4, border: 0, borderRadius: 7, background: current ? 'var(--surface-selection)' : 'transparent', textAlign: 'left', color: 'var(--text-primary)' }}
    >
      <span style={mono ? { fontSize: 'var(--t-body)' } : undefined}>{name}</span>
      {mono ? (
        <span className="mono" style={{ maxWidth: '100%', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
          {meta}
        </span>
      ) : (
        <span style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{meta}</span>
      )}
    </button>
  )
}
