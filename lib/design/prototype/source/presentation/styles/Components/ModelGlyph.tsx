import { Icon } from './Icon'

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
 * the Models place's Backends table; its glyphs are the icon set's (1q).
 */
export function ModelGlyph({ kind, size = 14, color: tint, label }: { readonly kind: GlyphKind; readonly size?: number; readonly color?: string; readonly label?: string }) {
  const color = kindColor(kind)
  if (!color) return null
  return <Icon name={kind === 'local' ? 'local' : kind === 'api' ? 'api' : 'subscription'} size={size} {...(label === undefined ? {} : { label, role: 'img' as const })} style={{ color: tint ?? color }} />
}
