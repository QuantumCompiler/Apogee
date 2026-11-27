import type { CSSProperties } from 'react'
import { Icon } from '../../styles/Components/Icon'
import type { TypeStep } from '../../styles/Tokens/Tokens'
import { FoundationsHeader, foundationsPage, sectionLabel } from '../Components/FoundationsHeader'
import type { Face, TypePageFace } from '../ViewModels/TypePage'

/**
 * Each type size's sample, what it is used for, its weight and whether it is
 * set in the code face: the old TypeSpace board's words, which tokens.json
 * does not hold (it holds the sizes), keyed by the token's own name.
 */
const steps: Readonly<Record<TypeStep, { readonly sample: string; readonly usedFor: string; readonly weight: number; readonly mono?: true; readonly caps?: true }>> = {
  display: { sample: 'What are we working on?', usedFor: 'Empty new session, empty library', weight: 600 },
  h1: { sample: 'Retrying timed-out fetches', usedFor: '# in answers', weight: 600 },
  h2: { sample: 'Option 1: catch it in the child', usedFor: '## in answers, sheet titles', weight: 600 },
  h3: { sample: 'When gather() is the better fit', usedFor: '### in answers, settings sections', weight: 600 },
  title: { sample: 'TaskGroup timeouts in the ingest runner', usedFor: 'Window title, card titles, model name', weight: 600 },
  prose: { sample: 'Every URL in the batch becomes its own task inside one TaskGroup.', usedFor: 'Answers and your messages', weight: 400 },
  body: { sample: 'Allow for this session', usedFor: 'Controls, settings rows, inspector values', weight: 400 },
  dense: { sample: 'Quantize Gemma 4 12B for the M3 Max', usedFor: 'Sidebar rows, menus, chips', weight: 400 },
  small: { sample: 'Warns at 80%. At 90%, older turns are summarized and the session notes it.', usedFor: 'Descriptions, notes, card footers', weight: 400 },
  code: { sample: 'async with asyncio.TaskGroup() as tg:', usedFor: 'Code blocks', weight: 400, mono: true },
  ui: { sample: 'fetch_url https://docs.python.org/3/library/asyncio-task.html', usedFor: 'Tool lines, paths, URLs, inline code', weight: 400, mono: true },
  meta: { sample: 'in 3,214 · out 588 · 14.1 s', usedFor: 'Timestamps, usage, shortcuts, notices', weight: 400, mono: true },
  label: { sample: 'THIS TURN · PREVIOUS 7 DAYS', usedFor: 'Section labels, all caps, +0.08 em', weight: 500, mono: true, caps: true },
  tag: { sample: 'LOCAL', usedFor: 'Model type tags, counts on badges', weight: 600, mono: true },
}

const section: CSSProperties = { marginTop: 48, paddingTop: 40, borderTop: '1px solid var(--line-border)' }
const h2: CSSProperties = { margin: '0 0 4px', fontSize: 'var(--t-h2)', fontWeight: 600 }
const lead: CSSProperties = { margin: '0 0 20px', color: 'var(--text-secondary)' }
const row: CSSProperties = { display: 'grid', alignItems: 'baseline', columnGap: 16, minHeight: 36, padding: '6px 0', borderBottom: '1px solid var(--line-border)' }
const mono: CSSProperties = { fontFamily: 'var(--font-mono)', fontSize: 'var(--t-ui)' }

/** A typeface and its sample, drawn in its own stack. */
function FaceCard({ face, sample }: { readonly face: Face; readonly sample: string }) {
  return (
    <div className="card" style={{ flex: '1 1 300px', padding: 16 }}>
      <div style={{ fontFamily: face.stack, fontSize: 'var(--t-display)', lineHeight: 1.2 }}>Aa</div>
      <div style={{ marginTop: 8, fontWeight: 600 }}>
        {face.name}
        {face.isDefault ? <span style={{ ...mono, marginLeft: 8, color: 'var(--status-accent)' }}>default</span> : null}
      </div>
      <div style={{ ...mono, color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{face.stack}</div>
      <div style={{ marginTop: 8, fontFamily: face.stack, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{sample}</div>
    </div>
  )
}

/**
 * The Foundations type, space and shape page (design 1q): the old TypeSpace
 * board, read from the tokens -- both typefaces and their alternates, the
 * 14 sizes at each text size, the line heights, the 13 spacing steps, the
 * radii, the control heights, the layout sizes, and the icons `styles`' set
 * holds.
 */
export function TypeSpacePage({ face }: { readonly face: TypePageFace }) {
  const medium = face.textSizes.indexOf('M')
  return (
    <div style={foundationsPage}>
      <FoundationsHeader label="FOUNDATIONS · TYPE, SPACE AND SHAPE" title="Type, spacing and shape">
        {face.intro}
      </FoundationsHeader>

      <section>
        <h2 style={sectionLabel}>INTERFACE · {face.faces.ui.length} FACES</h2>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 16 }}>
          {face.faces.ui.map((each) => (
            <FaceCard key={each.id} face={each} sample="ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 → · — ’ “ ”" />
          ))}
        </div>
        <h2 style={{ ...sectionLabel, marginTop: 24 }}>CODE · {face.faces.mono.length} FACES</h2>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 16 }}>
          {face.faces.mono.map((each) => (
            <FaceCard key={each.id} face={each} sample="0O 1lI rn m {}[]() => != -> ~/code/ingest/src 27,612 tok · 1.8 s" />
          ))}
        </div>
        <p style={{ ...lead, marginTop: 16 }}>IBM Plex Sans and Mono, the defaults, ship inside the app on macOS, Windows and Linux, so a layout measures the same everywhere. Settings › Appearance can swap either for the other faces here.</p>
      </section>

      <section style={section}>
        <h2 style={h2}>Type scale</h2>
        <p style={lead}>
          Sizes at Medium, the default. The last column shows where each size lands at Small, Medium, Large and Extra large. Nothing goes below {face.floor} px. Line heights: {face.lineHeights.map((line) => `${line.name} ${line.value}`).join(', ')}.
        </p>
        <div role="table" aria-label="Type sizes">
          <div role="row" className="mono" style={{ ...row, gridTemplateColumns: '90px minmax(0, 1fr) 60px 240px 150px', fontSize: 'var(--t-label)', letterSpacing: '.08em', color: 'var(--text-tertiary)' }}>
            <span role="columnheader">TOKEN</span>
            <span role="columnheader">SAMPLE</span>
            <span role="columnheader">SIZE</span>
            <span role="columnheader">USED FOR</span>
            <span role="columnheader">{face.textSizes.join(' · ')}</span>
          </div>
          {face.sizes.map((size) => {
            const step = steps[size.step]
            return (
              <div role="row" key={size.step} style={{ ...row, gridTemplateColumns: '90px minmax(0, 1fr) 60px 240px 150px' }}>
                <span role="rowheader" style={mono}>
                  {size.step}
                </span>
                <span role="cell" style={{ fontSize: `var(--t-${size.step})`, fontWeight: step.weight, fontFamily: step.mono ? 'var(--font-mono)' : 'var(--font-ui)', letterSpacing: step.caps ? '.08em' : undefined, overflowWrap: 'anywhere' }}>
                  {step.sample}
                </span>
                <span role="cell" style={mono}>
                  {size.px[medium]} px · {step.weight}
                </span>
                <span role="cell" style={{ color: 'var(--text-secondary)' }}>
                  {step.usedFor}
                </span>
                <span role="cell" style={{ ...mono, color: 'var(--text-tertiary)' }}>
                  {size.px.join(' · ')}
                </span>
              </div>
            )
          })}
        </div>
      </section>

      <section style={section}>
        <h2 style={h2}>Spacing</h2>
        <p style={lead}>
          {face.spacing.length} steps, from {face.spacing[0]} to {face.spacing.at(-1)} px.
        </p>
        {face.spacing.map((px) => (
          <div key={px} style={{ ...row, gridTemplateColumns: '60px minmax(0, 1fr)', alignItems: 'center' }}>
            <span style={mono}>{px} px</span>
            <span aria-hidden="true" style={{ display: 'block', width: px * 4, height: 10, borderRadius: 2, background: 'var(--status-accent)' }} />
          </div>
        ))}
      </section>

      <section style={section}>
        <h2 style={h2}>Shape</h2>
        <p style={lead}>Small radii, 1 px borders, and shadows only on things that float above the window.</p>
        <p style={{ ...sectionLabel, marginTop: 16 }}>RADIUS</p>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 20 }}>
          {face.radii.map((radius) => (
            <div key={radius.name} style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6, width: 88 }}>
              <span aria-hidden="true" style={{ width: 48, height: 48, boxSizing: 'border-box', border: '1px solid var(--line-strong)', background: 'var(--surface-control)', borderRadius: `var(--radius-${radius.name})` }} />
              <span style={mono}>{radius.name}</span>
              <span style={{ ...mono, color: 'var(--text-tertiary)' }}>{radius.value}</span>
            </div>
          ))}
        </div>
        <p style={{ ...sectionLabel, marginTop: 28 }}>CONTROL HEIGHT</p>
        {face.controls.map((control) => (
          <div key={control.name} style={{ ...row, gridTemplateColumns: '140px 60px minmax(0, 1fr)', alignItems: 'center' }}>
            <span style={mono}>{control.name}</span>
            <span style={{ ...mono, color: 'var(--text-tertiary)' }}>{control.px} px</span>
            <span aria-hidden="true" style={{ display: 'block', width: 160, height: control.px, boxSizing: 'border-box', border: '1px solid var(--line-strong)', borderRadius: 'var(--radius-control)', background: 'var(--surface-control)' }} />
          </div>
        ))}
        <p style={{ ...sectionLabel, marginTop: 28 }}>ELEVATION</p>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 20 }}>
          <div className="card" style={{ flex: '1 1 200px', padding: 14 }}>
            <div style={mono}>flat</div>
            <div style={{ marginTop: 4, color: 'var(--text-secondary)' }}>Color step plus a 1 px border. Everything in the window.</div>
          </div>
          {face.shadows.map((shadow) => (
            <div key={shadow.name} className="card" style={{ flex: '1 1 200px', padding: 14, boxShadow: `var(--shadow-${shadow.name})` }}>
              <div style={mono}>shadow.{shadow.name}</div>
              <div style={{ ...mono, marginTop: 4, color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{shadow.value}</div>
            </div>
          ))}
        </div>
      </section>

      <section style={section}>
        <h2 style={h2}>Window layout</h2>
        <p style={lead}>The window's measured sizes, in px.</p>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(260px, 1fr))', columnGap: 24 }}>
          {face.layout.map((size) => (
            <div key={size.name} style={{ ...row, gridTemplateColumns: 'minmax(0, 1fr) auto' }}>
              <span style={mono}>{size.name}</span>
              <span style={{ ...mono, color: 'var(--text-tertiary)' }}>{size.px}</span>
            </div>
          ))}
        </div>
      </section>

      <section style={section}>
        <h2 style={h2}>Icons</h2>
        <p style={lead}>
          One set, drawn for Apogee and shipped on every platform: 16 px grid, 1.5 px stroke, round caps. They take the color of their text.
        </p>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(120px, 1fr))', gap: 12 }}>
          {face.icons.map((name) => (
            <div key={name} style={{ display: 'flex', alignItems: 'center', gap: 8, minHeight: 28 }}>
              <Icon name={name} />
              <span style={mono}>{name}</span>
            </div>
          ))}
        </div>
      </section>
    </div>
  )
}
