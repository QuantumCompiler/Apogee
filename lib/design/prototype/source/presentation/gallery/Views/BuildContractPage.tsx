import type { CSSProperties, MouseEvent } from 'react'
import { CodeText, paint } from '../Components/CodeText'
import { FoundationsHeader, foundationsPage, sectionLabel } from '../Components/FoundationsHeader'
import type { Lead } from '../Models/contract'
import type { BuildContractViewModel, ContractChip } from '../ViewModels/BuildContractViewModel'

const section: CSSProperties = { marginTop: 44, paddingTop: 36, borderTop: '1px solid var(--line-border)' }
const h2: CSSProperties = { margin: '0 0 6px', fontSize: 'var(--t-h1)', fontWeight: 600 }
const intro: CSSProperties = { margin: '0 0 16px', maxWidth: 900, fontSize: 'var(--t-title)', color: 'var(--text-secondary)' }
const how: CSSProperties = { fontSize: 'var(--t-dense)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }
const mapRow: CSSProperties = { display: 'grid', gridTemplateColumns: 'minmax(220px, 1fr) minmax(300px, 1.5fr) 150px', gap: '6px 20px', alignItems: 'baseline', padding: '9px 0', borderBottom: '1px solid var(--line-border)', fontSize: 'var(--t-body)' }
const gapRow: CSSProperties = { display: 'grid', gridTemplateColumns: '48px minmax(0, 1fr) 90px', gap: '6px 16px', alignItems: 'baseline', padding: '12px 0', borderBottom: '1px solid var(--line-border)', fontSize: 'var(--t-body)', maxWidth: 1100 }
const list: CSSProperties = { margin: '8px 0 0', paddingLeft: 22, display: 'flex', flexDirection: 'column', gap: 10, fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }

/** A status chip, as the board draws it; a gap's is a link that brings its gap into view. */
function Chip({ chip }: { readonly chip: ContractChip }) {
  const style: CSSProperties = {
    display: 'inline-flex',
    alignItems: 'center',
    height: 18,
    padding: '0 6px',
    border: `1px ${chip.dashed ? 'dashed' : 'solid'} ${paint(chip.tone)}`,
    borderRadius: 'var(--radius-chip)',
    fontFamily: 'var(--font-mono)',
    fontSize: 'var(--t-label)',
    letterSpacing: '.04em',
    whiteSpace: 'nowrap',
    color: paint(chip.tone),
    textDecoration: 'none',
  }
  if (chip.anchor === undefined) return <span style={style}>{chip.label}</span>
  const anchor = chip.anchor
  // The gap's row on this page: the address is the gallery's, so the link scrolls rather than changing it.
  const show = (event: MouseEvent<HTMLAnchorElement>) => {
    event.preventDefault()
    event.currentTarget.closest('[data-build-page]')?.querySelector(`[id="${anchor}"]`)?.scrollIntoView?.({ block: 'center' })
  }
  return (
    <a href={`#${anchor}`} onClick={show} style={style}>
      {chip.label}
    </a>
  )
}

/** A step, a decision or a call: its lead in bold, the rest in the secondary text. */
function LeadItem({ lead }: { readonly lead: Lead }) {
  return (
    <li>
      <b style={{ color: 'var(--text-primary)', fontWeight: 600 }}>{lead.lead}</b> <CodeText text={lead.text} />
    </li>
  )
}

/**
 * The contract page (design 1r): the old BuildContract board, "how the app
 * talks to Apogee", over its typed data -- the stat cards counted, starting
 * up, the contract map by part with each status's chip and a gap's linked to
 * its gap, the gaps to close in the CLI, the decisions and the calls. Every
 * color is a token, so it follows its board's theme.
 */
export function BuildContractPage({ model }: { readonly model: BuildContractViewModel }) {
  const words = model.words
  return (
    <div data-build-page="contract" style={{ ...foundationsPage, paddingBottom: 64 }}>
      <FoundationsHeader label={words.eyebrow} title={words.title}>
        <CodeText text={words.intro} />
      </FoundationsHeader>

      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 12, marginTop: 24 }}>
        {model.cards().map((card) => (
          <div key={card.status} data-card={card.status} className="card" style={{ display: 'flex', alignItems: 'baseline', gap: 10, padding: '12px 16px', fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }}>
            <span className="mono" style={{ fontSize: 'var(--t-display)', fontWeight: 600, color: paint(card.tone) }}>
              {card.count}
            </span>
            <span>{card.words}</span>
          </div>
        ))}
      </div>

      <section style={section} aria-label={words.startingUp}>
        <h2 style={h2}>{words.startingUp}</h2>
        <ol style={{ ...list, margin: '12px 0 0', maxWidth: 980, gap: 8, fontSize: 'var(--t-title)' }}>
          {model.startingUp.map((step) => (
            <LeadItem key={step.lead} lead={step} />
          ))}
        </ol>
      </section>

      <section style={section} aria-label={words.map}>
        <h2 style={h2}>{words.map}</h2>
        <p style={intro}>
          {words.mapIntro}
          {model.legend().map((entry) => (
            <span key={entry.chip.label}>
              {' '}
              <Chip chip={entry.chip} /> {entry.words}
            </span>
          ))}
        </p>
        <div style={{ ...mapRow, padding: '0 0 8px', borderBottom: '1px solid var(--line-strong)', fontFamily: 'var(--font-mono)', fontSize: 'var(--t-label)', letterSpacing: '.08em', color: 'var(--text-tertiary)' }}>
          {words.head.map((head) => (
            <div key={head}>{head}</div>
          ))}
        </div>
        {model.parts().map((part) => (
          <div key={part.part}>
            <h3 style={{ ...sectionLabel, margin: '26px 0 0', paddingBottom: 8, borderBottom: '1px solid var(--line-strong)', fontWeight: 500 }}>{part.part}</h3>
            {part.rows.map((row, index) => (
              <div key={index} style={mapRow}>
                <div>
                  <CodeText text={row.shows} />
                </div>
                <div style={how}>
                  <CodeText text={row.how} />
                </div>
                <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
                  <Chip chip={row.chip} />
                </div>
              </div>
            ))}
          </div>
        ))}
      </section>

      <section style={section} aria-label={words.gaps}>
        <h2 style={h2}>{words.gaps}</h2>
        <p style={intro}>{words.gapsIntro}</p>
        {model.gapRows().map((gap) => (
          <div key={gap.id} id={gap.anchor} style={{ ...gapRow, ...(gap.closed ? { opacity: 0.6 } : {}) }}>
            <span className="mono" style={{ fontSize: 'var(--t-small)', fontWeight: 600, color: gap.closed ? 'var(--status-accent)' : 'var(--status-warning)' }}>
              {gap.id}
            </span>
            <div>
              <div style={{ fontWeight: 600 }}>
                <CodeText text={gap.title} />
              </div>
              <div style={{ ...how, marginTop: 3 }}>
                <CodeText text={gap.change} />
              </div>
              <div style={{ marginTop: 4, fontSize: 'var(--t-small)', color: gap.closed ? 'var(--status-accent)' : 'var(--text-tertiary)' }}>
                <CodeText text={gap.until} />
              </div>
            </div>
            <span style={{ fontSize: 'var(--t-small)', textAlign: 'right', color: gap.closed ? 'var(--status-accent)' : 'var(--text-secondary)' }}>{gap.when}</span>
          </div>
        ))}
      </section>

      <div style={{ ...section, display: 'flex', flexWrap: 'wrap', gap: '40px 64px', alignItems: 'flex-start' }}>
        <section style={{ flex: '1 1 640px', minWidth: 0 }} aria-label={words.decisions}>
          <h2 style={h2}>{words.decisions}</h2>
          <p style={intro}>{words.decisionsIntro}</p>
          <ol style={list}>
            {model.decisions.map((decision) => (
              <LeadItem key={decision.lead} lead={decision} />
            ))}
          </ol>
        </section>
        <section style={{ flex: '1 1 420px', minWidth: 0 }} aria-label={words.calls}>
          <h2 style={h2}>{words.calls}</h2>
          <p style={intro}>{words.callsIntro}</p>
          <ol style={list}>
            {model.calls.map((call) => (
              <LeadItem key={call.lead} lead={call} />
            ))}
          </ol>
        </section>
      </div>
    </div>
  )
}
