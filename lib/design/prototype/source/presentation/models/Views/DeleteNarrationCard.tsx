/**
 * What a delete said beyond what it removed, kept until dismissed (design
 * 1h): the old prototype's "Deleted …" card (`main.html` at `b375fac9`,
 * `mdl.hasNarr`, `st.hasNarr`), every line as the CLI printed it -- the Mac
 * app's `DeleteNarrationCard`.
 */
export function DeleteNarrationCard({ title, text, dismiss }: { readonly title: string; readonly text: string; readonly dismiss: () => void }) {
  return (
    <div className="card" role="status" style={{ margin: '8px 0 6px', padding: '14px 16px' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
        <span style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-body)', fontWeight: 600, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{title}</span>
        <button type="button" className="btn-g" onClick={dismiss} style={{ height: 26, fontSize: 'var(--t-small)' }}>
          Dismiss
        </button>
      </div>
      <div className="mono" style={{ marginTop: 10, padding: '8px 10px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
        {text}
      </div>
    </div>
  )
}
