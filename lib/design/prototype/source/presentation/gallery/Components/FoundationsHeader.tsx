import type { CSSProperties, ReactNode } from 'react'

/** A Foundations or Build page's frame, as the old boards set it: the window's surface, the interface face and size, a 48 px margin. */
export const foundationsPage: CSSProperties = {
  boxSizing: 'border-box',
  maxWidth: 1440,
  margin: '0 auto',
  padding: '48px 48px 56px',
  background: 'var(--surface-window)',
  color: 'var(--text-primary)',
  fontFamily: 'var(--font-ui)',
  fontSize: 'var(--t-body)',
  lineHeight: 1.5,
}

/** A section's label, in capitals and the code face, as the old boards' `.lbl`. */
export const sectionLabel: CSSProperties = { margin: '0 0 10px', fontFamily: 'var(--font-mono)', fontSize: 'var(--t-label)', fontWeight: 400, letterSpacing: '.08em', color: 'var(--text-tertiary)' }

/**
 * A Foundations page's header (design 1q), as the old boards draw it: the
 * label over the title, and what the page is in a sentence or two. The Build
 * pages (1r) draw theirs with it too.
 */
export function FoundationsHeader({ label, title, children }: { readonly label: string; readonly title: string; readonly children: ReactNode }) {
  return (
    <header style={{ maxWidth: 820, marginBottom: 32 }}>
      <div className="mono" style={{ fontSize: 'var(--t-meta)', letterSpacing: '.08em', color: 'var(--text-tertiary)' }}>
        {label}
      </div>
      <h1 style={{ margin: '6px 0 10px', fontSize: 'var(--t-display)', lineHeight: 1.2, fontWeight: 600, letterSpacing: '-.01em' }}>{title}</h1>
      <p style={{ margin: 0, fontSize: 'var(--t-prose)', lineHeight: 1.6, color: 'var(--text-secondary)' }}>{children}</p>
    </header>
  )
}
