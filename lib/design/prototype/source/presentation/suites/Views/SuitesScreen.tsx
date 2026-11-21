import { useObserved } from '../../styles/Observation/useObserved'
import type { SuiteDetailFace } from '../ViewModels/SuitesFaces'
import type { SuitesViewModel } from '../ViewModels/SuitesViewModel'

const toneColor = { fits: 'var(--status-accent)', over: 'var(--status-error)', plain: 'var(--text-secondary)' } as const
const columns = '120px minmax(0, 160px) 90px minmax(0, 1fr)'

/**
 * The Suites place (design 1p, ⌘3): the old prototype's screen (`main.html`
 * at `b375fac9`, `ui.isSuites`) -- its header with New suite…, the 280 px
 * list of suites with the default's badge and each one's verdict, and the
 * chosen suite's page -- drawn from the view-model's face.
 */
export function SuitesScreen({ model }: { readonly model: SuitesViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 14, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Suites</h1>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>A main model plus small helpers, loaded together</span>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="btn-g" onClick={() => model.newSuite()}>
          New suite…
        </button>
      </header>
      <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
        <nav aria-label="Suites" style={{ flex: '0 0 280px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', borderRight: '1px solid var(--line-border)' }}>
          {face.list.map((row) => (
            <button
              key={row.name}
              type="button"
              className="hv"
              onClick={() => model.select(row.name)}
              aria-current={row.current}
              style={{ width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '10px 12px', marginBottom: 4, border: 0, borderRadius: 7, background: row.current ? 'var(--surface-selection)' : '', color: 'var(--text-primary)', textAlign: 'left' }}
            >
              <span style={{ display: 'flex', alignItems: 'center', gap: 8, width: '100%' }}>
                <span style={{ flex: '1 1 auto', fontSize: 'var(--t-body)', fontWeight: 500 }}>{row.name}</span>
                {row.isDefault ? (
                  <span className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--status-accent)' }}>
                    default
                  </span>
                ) : null}
              </span>
              <span className="mono" style={{ fontSize: 'var(--t-meta)', color: row.over ? 'var(--status-error)' : 'var(--text-tertiary)' }}>
                {row.summary}
              </span>
            </button>
          ))}
          {face.empty !== '' ? <p style={{ margin: '6px 12px', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{face.empty}</p> : null}
        </nav>
        <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '22px 28px 32px' }}>{face.detail.shown ? <SuiteDetail model={model} detail={face.detail} /> : null}</div>
      </div>
    </div>
  )
}

/** The chosen suite's page: its name, the default's button, the two session actions, the CLI's total, its members and the footer naming the read. */
export function SuiteDetail({ model, detail }: { readonly model: SuitesViewModel; readonly detail: SuiteDetailFace }) {
  return (
    <div style={{ maxWidth: 860 }}>
      <div style={{ display: 'flex', alignItems: 'flex-start', flexWrap: 'wrap', gap: 12 }}>
        <div style={{ flex: '1 1 320px', minWidth: 0 }}>
          <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{detail.name}</h2>
          {detail.desc !== '' ? <p style={{ margin: '4px 0 0', color: 'var(--text-secondary)' }}>{detail.desc}</p> : null}
        </div>
        <button type="button" className="btn-g" onClick={() => void model.makeDefault()}>
          {detail.defaultLabel}
        </button>
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8, marginTop: 14 }}>
        <button type="button" className="btn-p" onClick={() => model.openChatUnderIt()} title={detail.openHelp}>
          {detail.openLabel}
        </button>
        <button type="button" className="btn" onClick={() => model.switchSessionToIt()} disabled={!detail.canSwitch} title={detail.switchHelp}>
          {detail.switchLabel}
        </button>
      </div>
      <p style={{ margin: '22px 0 0', fontSize: 'var(--t-body)', lineHeight: 1.5, color: toneColor[detail.tone], overflowWrap: 'anywhere' }}>{detail.total}</p>
      <div className="lbl" style={{ display: 'grid', gridTemplateColumns: columns, columnGap: 12, marginTop: 18, paddingBottom: 6, borderBottom: '1px solid var(--line-strong)' }}>
        <span>ROLE</span>
        <span>MODEL</span>
        <span>WINDOW</span>
        <span>HOLDS</span>
      </div>
      {detail.members.map((member) => (
        <div key={member.backend} style={{ display: 'grid', gridTemplateColumns: columns, columnGap: 12, alignItems: 'baseline', padding: '10px 0', borderBottom: '1px solid var(--line-border)', fontSize: 'var(--t-small)' }}>
          <span style={{ color: 'var(--text-secondary)' }}>{member.role}</span>
          <span className="mono" style={{ fontSize: 'var(--t-ui)', overflowWrap: 'anywhere' }}>
            {member.backend}
          </span>
          <span style={{ color: 'var(--text-secondary)' }}>{member.window}</span>
          <span style={{ color: 'var(--text-primary)', overflowWrap: 'anywhere' }}>{member.holds}</span>
        </div>
      ))}
      {detail.unread.map((line) => (
        <div key={line} className="mono" style={{ marginTop: 8, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          {line}
        </div>
      ))}
      <p style={{ margin: '14px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{detail.footer}</p>
    </div>
  )
}
