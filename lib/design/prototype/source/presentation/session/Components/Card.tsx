import type { ReactNode } from 'react'

/**
 * The session's card (design 1v): a raised surface with the design's border,
 * accented for what it asks -- the old prototype's permission, question,
 * failed-load, not-sent and ended boxes (`main.html:370`, `:508`, `:550`,
 * `:662`, `:671` at `b375fac9`). The Mac app's `Card`. `padded` gives it the
 * old not-sent and ended boxes' padding and column; the others lay out their
 * own parts edge to edge.
 */
export function Card({ accent = 'var(--line-strong)', padded = false, role, label, children }: {
  readonly accent?: string
  readonly padded?: boolean
  readonly role?: 'group' | 'alert'
  readonly label?: string
  readonly children: ReactNode
}) {
  return (
    <div
      role={role ?? 'group'}
      aria-label={label}
      style={{
        border: `1px solid ${accent}`,
        borderRadius: 8,
        background: 'var(--surface-card)',
        overflow: 'hidden',
        ...(padded ? { display: 'flex', flexDirection: 'column', alignItems: 'flex-start', gap: 8, padding: '12px 14px' } : {}),
      }}
    >
      {children}
    </div>
  )
}

/** A small capital label above a card's words, the design's label style (`lbl`). The Mac app's `CardLabel`. */
export function CardLabel({ text }: { readonly text: string }) {
  return <span className="lbl">{text.toUpperCase()}</span>
}
