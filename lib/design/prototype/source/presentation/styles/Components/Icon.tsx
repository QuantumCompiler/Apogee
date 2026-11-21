import type { CSSProperties } from 'react'
import { iconSet } from '../Icons/IconSet'
import type { IconName } from '../Icons/IconNames'

/**
 * One of the app's icons (design 1q): its glyph from the set, on the 16 px
 * grid at `size`, in the color of its text. Hidden from a screen reader,
 * unless it says something its words do not (`label`, with `role` where it
 * is read as a picture).
 */
export function Icon({ name, size = 16, style, label, role }: { readonly name: IconName; readonly size?: number; readonly style?: CSSProperties; readonly label?: string; readonly role?: 'img' }) {
  const glyph = iconSet[name]
  return (
    <svg className={glyph.filled ? undefined : 'i'} width={size} height={size} viewBox="0 0 16 16" {...(label === undefined ? { 'aria-hidden': true } : { 'aria-label': label, ...(role ? { role } : {}) })} style={style}>
      {glyph.paths}
    </svg>
  )
}
