/** Where a model runs, as the glyph marks it: the reads' `ModelKind`, or none known (the mock backend's). */
export type GlyphKind = 'local' | 'api' | 'subscription' | 'unknown' | 'none'

/** The color a model's kind is marked in, as the old prototype's `kindColor`; none where the kind is not known. */
export function kindColor(kind: GlyphKind): string | undefined {
  switch (kind) {
    case 'local':
      return 'var(--model-local)'
    case 'api':
      return 'var(--model-api)'
    case 'subscription':
      return 'var(--model-subscription)'
    default:
      return undefined
  }
}

/**
 * A model's glyph, by its kind: the chip, the cylinder, the circling arrows
 * (`main.html:185-187` and `:883-885` at `b375fac9`). The session list (1g)
 * draws a local row's chip quiet unless it is the open session's (`color`),
 * and names each glyph for a screen reader (`label`), as the old list does.
 * Promoted here from the session module by design 1h, its second user being
 * the Models place's Backends table.
 */
export function ModelGlyph({ kind, size = 14, color: tint, label }: { readonly kind: GlyphKind; readonly size?: number; readonly color?: string; readonly label?: string }) {
  const color = kindColor(kind)
  if (!color) return null
  return (
    <svg className="i" width={size} height={size} viewBox="0 0 16 16" {...(label === undefined ? { 'aria-hidden': true } : { 'aria-label': label, role: 'img' })} style={{ color: tint ?? color }}>
      {kind === 'local' ? (
        <>
          <rect x="4" y="4" width="8" height="8" rx="1.5" />
          <path d="M6.5 2v2M9.5 2v2M6.5 12v2M9.5 12v2M2 6.5h2M2 9.5h2M12 6.5h2M12 9.5h2" />
        </>
      ) : kind === 'api' ? (
        <>
          <ellipse cx="8" cy="4.5" rx="5" ry="2" />
          <path d="M3 4.5V8c0 1.1 2.2 2 5 2s5-.9 5-2V4.5" />
          <path d="M3 8v3.5c0 1.1 2.2 2 5 2s5-.9 5-2V8" />
        </>
      ) : (
        <>
          <path d="M12.5 6A4.75 4.75 0 0 0 3.6 5.2" />
          <path d="M3.5 10a4.75 4.75 0 0 0 8.9.8" />
          <path d="M3.3 2.6v2.8h2.8" />
          <path d="M12.7 13.4v-2.8H9.9" />
        </>
      )}
    </svg>
  )
}
