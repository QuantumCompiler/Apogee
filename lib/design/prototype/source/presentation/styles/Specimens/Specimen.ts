import type { ReactNode } from 'react'

/**
 * The four sections the old Components boards drew (design 1q), in their
 * order: Model, context and sources; The turn; Asking and failing; Making
 * media (the boards `ComponentsModel`, `ComponentsTurn`, `ComponentsPrompts`
 * and `ComponentsMedia` at `b375fac9`).
 */
export type SpecimenSection = 'model' | 'turn' | 'asking' | 'media'

/** The sections in the boards' order. */
export const specimenSections: readonly SpecimenSection[] = ['model', 'turn', 'asking', 'media']

/**
 * A component as the gallery's Components pages show it (design 1q): a
 * feature module's own component, drawn over a view-model built on the
 * stand-in's recordings -- a named preview of its module reused where one
 * fits -- never a copy. Each module's `Previews/Specimens.tsx` exports its
 * own; `app` gathers them and hands them to the gallery, which imports no
 * place. The gallery draws each in a card themed by the board's theme.
 */
export interface Specimen {
  /** What it is, in the old board's words where it had them: "Context meter · 80% and over". */
  readonly name: string
  readonly section: SpecimenSection
  /** The module whose component it is: `session`, `styles`, `video`. */
  readonly module: string
  /** The design item that ported it, for the card that stands in while a section has none. */
  readonly item: string
  /** Draws it. A component, so it may build its view-model with hooks as a preview does. */
  readonly render: () => ReactNode
}
