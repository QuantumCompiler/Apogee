/**
 * The plain card a board shows while what it draws has not landed (design
 * 1q's default): "Not ported yet", and the design item that ports it, so a
 * feature item never edits the gallery -- its board goes live when its
 * preset, page or specimen registers. The Build row's Contract, Layout and
 * Processes showed it until 1r's pages landed; today only a Components
 * section with no specimen would.
 */
export function NotPortedCard({ item, title }: { readonly item: string; readonly title: string }) {
  return (
    <div role="note" aria-label={`${title}: not ported yet`} style={{ height: '100%', boxSizing: 'border-box', padding: 48, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 }}>
      <div className="card" style={{ maxWidth: 560, padding: '20px 24px' }}>
        <div className="mono" style={{ fontSize: 'var(--t-label)', letterSpacing: '.08em', color: 'var(--text-tertiary)' }}>
          {title.toUpperCase()}
        </div>
        <p style={{ margin: '8px 0 0', fontSize: 'var(--t-prose)' }}>Not ported yet: item {item}</p>
      </div>
    </div>
  )
}
