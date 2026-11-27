/**
 * The said-card (GUI M22): what the CLI said, every line as printed, under a
 * title, until dismissed -- the Mac app's `SaidCard` (`PresentationStyles`).
 * Neutral -- secondary text in the code font on the code background, never a
 * warning colour -- since the same voice carries bad news, plain notes and
 * good news. Promoted from Models' delete narration card (design 1h, the old
 * prototype's "Deleted …" card, `main.html` at `b375fac9`, `mdl.hasNarr`,
 * `st.hasNarr`) once Knowledge and the Code graph drew a run's notes on it.
 */
export function SaidCard({ title, lines, dismiss }: { readonly title: string; readonly lines: readonly string[]; readonly dismiss: () => void }) {
  return (
    <div className="card" role="status" style={{ margin: '8px 0 6px', padding: '14px 16px' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
        <span style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-body)', fontWeight: 600, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{title}</span>
        <button type="button" className="btn-g" onClick={dismiss} style={{ height: 26, fontSize: 'var(--t-small)' }}>
          Dismiss
        </button>
      </div>
      <div className="mono" style={{ marginTop: 10, padding: '8px 10px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
        {lines.join('\n')}
      </div>
    </div>
  )
}
