import type { CSSProperties } from 'react'
import { CodeText, paint } from '../Components/CodeText'
import { FoundationsHeader, foundationsPage } from '../Components/FoundationsHeader'
import type { BuildLayoutViewModel } from '../ViewModels/BuildLayoutViewModel'

const section: CSSProperties = { marginTop: 44, paddingTop: 36, borderTop: '1px solid var(--line-border)' }
const h2: CSSProperties = { margin: '0 0 6px', fontSize: 'var(--t-h1)', fontWeight: 600 }
const intro: CSSProperties = { margin: '0 0 16px', maxWidth: 900, fontSize: 'var(--t-title)', color: 'var(--text-secondary)' }
const ruled: CSSProperties = { padding: '9px 0', borderBottom: '1px solid var(--line-border)' }
const mono: CSSProperties = { fontFamily: 'var(--font-mono)', fontSize: '.92em' }
const pair: CSSProperties = { ...ruled, display: 'grid', gridTemplateColumns: '150px minmax(0, 1fr)', gap: '4px 16px', alignItems: 'baseline' }
const fourColumns: CSSProperties = { ...ruled, display: 'grid', gridTemplateColumns: 'minmax(180px, 1fr) minmax(200px, 1.2fr) minmax(180px, 1fr) minmax(180px, 1fr)', gap: '4px 18px', alignItems: 'baseline' }
const twoUp: CSSProperties = { ...section, display: 'flex', flexWrap: 'wrap', gap: '40px 64px', alignItems: 'flex-start' }

/**
 * The layout page (design 1r): the old BuildLayout board, "layout, navigation
 * and native controls" -- the window at three quarters, the regions and their
 * sizes, the keys, the states every screen draws, the native controls, the
 * type sizes and space, shape and motion -- every size read from the tokens
 * and every key from the key map, through its view-model.
 */
export function BuildLayoutPage({ model }: { readonly model: BuildLayoutViewModel }) {
  const face = model.face()
  const words = face.words
  return (
    <div data-build-page="layout" style={{ ...foundationsPage, paddingBottom: 64 }}>
      <FoundationsHeader label={words.eyebrow} title={words.title}>
        <CodeText text={words.intro} />
      </FoundationsHeader>

      <section style={section}>
        <h2 style={h2}>{words.window}</h2>
        <p style={intro}>{words.windowIntro}</p>
        <div style={{ position: 'relative', width: face.window.width, height: face.window.height, maxWidth: '100%' }}>
          {face.window.boxes.map((box) => (
            <div
              key={box.part}
              data-part={box.part}
              style={{ position: 'absolute', left: box.x, top: box.y, width: box.width, height: box.height, boxSizing: 'border-box', padding: '8px 10px', display: 'flex', alignItems: 'flex-start', background: paint(box.surface), border: `1px solid ${paint(box.line)}`, borderRadius: 'var(--radius-chip)', fontSize: 'var(--t-small)', lineHeight: '16px', color: 'var(--text-primary)' }}
            >
              <span>
                <b>{box.title}</b>
                {box.note === undefined ? null : (
                  <>
                    <br />
                    <span style={{ color: 'var(--text-secondary)' }}>{box.note}</span>
                  </>
                )}
              </span>
            </div>
          ))}
        </div>
      </section>

      <section style={section}>
        <h2 style={h2}>{words.regions}</h2>
        <p style={intro}>{words.regionsIntro}</p>
        {face.regions.map((region) => (
          <div key={region.name} style={{ ...ruled, display: 'grid', gridTemplateColumns: '150px 170px minmax(0, 1fr)', gap: '4px 16px', alignItems: 'baseline', maxWidth: 1100 }}>
            <b>{region.name}</b>
            <span style={mono}>{region.size}</span>
            <span style={{ color: 'var(--text-secondary)' }}>{region.words}</span>
          </div>
        ))}
      </section>

      <div style={twoUp}>
        <section style={{ flex: '1 1 560px', minWidth: 0 }}>
          <h2 style={h2}>{words.keys}</h2>
          <p style={intro}>{words.keysIntro}</p>
          {face.keys.map((row) => (
            <div key={row.words} style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: 12, alignItems: 'center', padding: '8px 0', borderBottom: '1px solid var(--line-border)' }}>
              <span>{row.words}</span>
              <span style={{ padding: '1px 7px', border: '1px solid var(--line-strong)', borderRadius: 'var(--radius-chip)', fontFamily: 'var(--font-mono)', fontSize: 'var(--t-ui)', color: 'var(--text-secondary)', whiteSpace: 'nowrap' }}>{row.keys}</span>
            </div>
          ))}
        </section>
        <section style={{ flex: '1 1 520px', minWidth: 0 }}>
          <h2 style={h2}>{words.states}</h2>
          <p style={intro}>{words.statesIntro}</p>
          {face.states.map((state) => (
            <div key={state.name} style={pair}>
              <b>{state.name}</b>
              <span style={{ color: 'var(--text-secondary)' }}>{state.words}</span>
            </div>
          ))}
        </section>
      </div>

      <section style={section}>
        <h2 style={h2}>{words.controls}</h2>
        <p style={intro}>{words.controlsIntro}</p>
        <div style={{ ...fourColumns, padding: '0 0 8px', borderBottom: '1px solid var(--line-strong)', fontFamily: 'var(--font-mono)', fontSize: 'var(--t-label)', letterSpacing: '.08em', color: 'var(--text-tertiary)' }}>
          <span>{face.controls.head.design}</span>
          <span>{face.controls.head.swiftui}</span>
          <span>{face.controls.head.qtQuick}</span>
          <span>{face.controls.head.winui}</span>
        </div>
        {face.controls.rows.map((control) => (
          <div key={control.design} style={fourColumns}>
            <b>{control.design}</b>
            <span style={{ ...mono, color: 'var(--text-secondary)' }}>{control.swiftui}</span>
            <span style={{ ...mono, color: 'var(--text-secondary)' }}>{control.qtQuick}</span>
            <span style={{ ...mono, color: 'var(--text-secondary)' }}>{control.winui}</span>
          </div>
        ))}
      </section>

      <div style={twoUp}>
        <section style={{ flex: '1 1 520px', minWidth: 0 }}>
          <h2 style={h2}>{words.type}</h2>
          <p style={intro}>{words.typeIntro}</p>
          {face.type.map((size) => (
            <div key={size.step} style={{ display: 'grid', gridTemplateColumns: '100px minmax(0, 1fr) 60px', gap: 12, alignItems: 'baseline', padding: '6px 0', borderBottom: '1px solid var(--line-border)' }}>
              <span style={{ ...mono, color: 'var(--text-tertiary)' }}>{size.variable}</span>
              <span style={{ fontSize: `var(${size.variable})` }}>{size.sample}</span>
              <span style={{ ...mono, textAlign: 'right', color: 'var(--text-tertiary)' }}>{size.px}</span>
            </div>
          ))}
        </section>
        <section style={{ flex: '1 1 520px', minWidth: 0 }}>
          <h2 style={h2}>{words.space}</h2>
          {face.space.map((row) => (
            <div key={row.name} style={pair}>
              <b>{row.name}</b>
              <span style={{ color: 'var(--text-secondary)' }}>{row.words}</span>
            </div>
          ))}
        </section>
      </div>
    </div>
  )
}
