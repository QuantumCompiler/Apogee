import { specimenSections } from '../../styles/Specimens/Specimen'
import type { Specimen, SpecimenSection } from '../../styles/Specimens/Specimen'
import { FoundationsHeader, foundationsPage, sectionLabel } from '../Components/FoundationsHeader'
import type { BoardEntry } from '../Models/Boards'
import { NotPortedCard } from './NotPortedCard'

/** What each section is about: the old Components boards' opening lines, word for word. */
const intros: Readonly<Record<SpecimenSection, string>> = {
  model: 'What is answering, how much room it has left, and what it is reading from.',
  turn: 'Everything a model does between your message and its answer: thinking, the helpers it consulted, the tools it ran, and what the app did on your behalf.',
  asking: 'The moments a turn stops for you: permission to act, a question only you can answer, and an error with its fix.',
  media: 'The pieces of Video and Voice: the player, a run’s progress, a clip’s record and history, the generate form’s controls, a composition’s timeline and scenes, the edit preview, voices, and the install prompt. Every value here is one a CLI document or command carries.',
}

/** Each section's heading: the old Components boards' h1, word for word (the canvas's board title is the row's label, not the page's heading). */
const headings: Readonly<Record<SpecimenSection, string>> = {
  model: 'Model, context and sources',
  turn: 'The turn',
  asking: 'Asking and failing',
  media: 'Making media',
}

/** One specimen in its card: its name, and the module's own component drawn below it. */
function SpecimenCard({ specimen }: { readonly specimen: Specimen }) {
  const Render = specimen.render
  return (
    <figure className="card" style={{ margin: 0, minWidth: 0, overflow: 'hidden' }}>
      <figcaption className="mono" style={{ padding: '8px 12px', borderBottom: '1px solid var(--line-border)', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
        {specimen.name}
      </figcaption>
      <div style={{ padding: 16, overflowX: 'auto', background: 'var(--surface-window)' }}>
        <Render />
      </div>
    </figure>
  )
}

/**
 * A Foundations Components page (design 1q): one of the old four boards'
 * sections, drawn from the real components -- each feature module's own,
 * as its `Previews/Specimens.tsx` exports them over the CLI's recordings,
 * gathered by `app` -- by module, where the old boards kept static copies in
 * step by hand. A section no ported module draws shows the "Not ported yet"
 * card, naming the item that ports it.
 */
export function ComponentsPage({ entry, specimens }: { readonly entry: BoardEntry; readonly specimens: readonly Specimen[] }) {
  const section = entry.section ?? 'model'
  const mine = specimens.filter((specimen) => specimen.section === section)
  const modules = [...new Set(mine.map((specimen) => specimen.module))]
  const title = headings[section]
  return (
    <div style={foundationsPage}>
      <FoundationsHeader label={`FOUNDATIONS · COMPONENTS ${specimenSections.indexOf(section) + 1} OF ${specimenSections.length}`} title={title}>
        {intros[section]} Each is its module’s own component, drawn over the CLI’s recordings.
      </FoundationsHeader>
      {modules.length === 0 ? <NotPortedCard item={entry.item ?? ''} title={title} /> : null}
      {modules.map((module) => {
        const own = mine.filter((specimen) => specimen.module === module)
        return (
          <section key={module} aria-label={module} style={{ marginTop: 32 }}>
            <h2 style={sectionLabel}>
              {module.toUpperCase()} · {own.length} {own.length === 1 ? 'COMPONENT' : 'COMPONENTS'}
            </h2>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(420px, 1fr))', gap: 16, alignItems: 'start' }}>
              {own.map((specimen) => (
                <SpecimenCard key={specimen.name} specimen={specimen} />
              ))}
            </div>
          </section>
        )
      })}
    </div>
  )
}
