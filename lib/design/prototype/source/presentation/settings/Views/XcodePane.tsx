import { useObserved } from '../../styles/Observation/useObserved'
import { SectionHeading } from '../Components/SettingRow'
import type { XcodeSettings } from '../ViewModels/XcodeSettings'

const meta = { margin: '8px 0 0', fontSize: 'var(--t-meta)' } as const

/** Writes a field's value to the clipboard, where the page may: a file opened from disk may be refused it. The toast says Copy was pressed either way, as the old pane says it. */
function copy(value: string, done: () => void): void {
  try {
    void navigator.clipboard?.writeText(value).catch(() => undefined)
  } catch {
    // No clipboard here: the value stays on screen to select.
  }
  done()
}

/**
 * Xcode (design 1i): the two routes joining Apogee and Xcode, each in the
 * CLI's words -- what `check` says of Xcode, Xcode's tools for Apogee's
 * agents (Register, or Remove…), and the fields Xcode's Add an Agent sheet
 * takes, each with Copy. The app writes nothing into Xcode.
 */
export function XcodePane({ model }: { readonly model: XcodeSettings }) {
  useObserved(model)
  const face = model.face()
  return (
    <>
      <p style={{ margin: '16px 0 6px', fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }}>Two routes join Apogee and Xcode, each in apogee’s own words. The app writes nothing into Xcode: you add Apogee in Xcode’s own sheet.</p>
      <SectionHeading>WHAT CHECK SAYS OF XCODE</SectionHeading>
      {face.rowsLacking !== '' ? <p style={{ margin: '10px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{face.rowsLacking}</p> : null}
      {face.rows.map((row) => (
        <div key={row.text} style={{ display: 'flex', gap: 8, padding: '6px 0', borderBottom: '1px solid var(--line-border)' }}>
          <span aria-hidden="true" style={{ color: row.markColor }}>
            {row.mark}
          </span>
          <span style={{ fontSize: 'var(--t-small)', color: row.color, overflowWrap: 'anywhere' }}>
            {row.text}
            {row.remedy !== '' ? (
              <span className="mono" style={{ display: 'block', marginTop: 2, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>
                {row.remedy}
              </span>
            ) : null}
          </span>
        </div>
      ))}
      <SectionHeading margin="24px 0 0">LET APOGEE USE XCODE’S TOOLS</SectionHeading>
      {face.tools.lacking !== '' ? <p style={{ margin: '10px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{face.tools.lacking}</p> : null}
      {face.tools.description !== '' ? <p style={{ margin: '10px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{face.tools.description}</p> : null}
      {face.tools.state !== '' ? <p style={{ margin: '8px 0 0', fontSize: 'var(--t-body)', color: face.tools.stateColor, overflowWrap: 'anywhere' }}>{face.tools.state}</p> : null}
      {face.tools.missing !== '' ? <p style={{ ...meta, color: 'var(--status-warning)' }}>{face.tools.missing}</p> : null}
      {face.tools.note !== '' ? <p style={{ ...meta, color: 'var(--text-secondary)' }}>{face.tools.note}</p> : null}
      {face.tools.canRegister ? (
        <>
          <p style={{ ...meta, color: 'var(--text-tertiary)' }}>Register writes Apogee’s config and connects to nothing: Xcode is first reached when a session starts the server, and asks you then.</p>
          <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 10, marginTop: 10 }}>
            <button type="button" className="btn" onClick={() => void model.register()} disabled={face.tools.working}>
              Register
            </button>
            <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
              {face.tools.command}
            </span>
          </div>
        </>
      ) : null}
      {face.tools.canRemove ? (
        <div style={{ marginTop: 10 }}>
          <button type="button" className="btn-g" onClick={() => model.askToRemove()} disabled={face.tools.working}>
            Remove…
          </button>
        </div>
      ) : null}
      {face.failure !== '' ? <p style={{ margin: '8px 0 0', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{face.failure}</p> : null}
      {face.said.shown ? (
        <div style={{ marginTop: 12, padding: '10px 12px', border: '1px solid var(--line-border)', borderRadius: 'var(--radius-control)' }}>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}>
            <span style={{ flex: '1 1 auto', fontSize: 'var(--t-small)', fontWeight: 600 }}>{face.said.title}</span>
            <button type="button" className="btn-g" onClick={() => model.dismissSaid()} style={{ height: 24, fontSize: 'var(--t-small)' }}>
              Dismiss
            </button>
          </div>
          <div className="mono" style={{ marginTop: 6, fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
            {face.said.text}
          </div>
        </div>
      ) : null}
      <SectionHeading margin="24px 0 0">USE APOGEE AS AN AGENT IN XCODE</SectionHeading>
      {face.agent.lacking !== '' ? <p style={{ margin: '10px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{face.agent.lacking}</p> : null}
      {face.agent.notOffered ? <p style={{ margin: '10px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>Not offered: check’s Xcode rows above say why.</p> : null}
      {face.agent.heading !== '' ? <p style={{ margin: '10px 0 6px', fontSize: 'var(--t-small)' }}>{face.agent.heading}</p> : null}
      {face.agent.fields.map((field, index) => (
        <div key={`${field.label} ${index}`} style={{ display: 'grid', gridTemplateColumns: '170px minmax(0, 1fr) 64px', gap: 12, alignItems: 'center', minHeight: 34, padding: '4px 0', borderBottom: '1px solid var(--line-border)' }}>
          <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{field.label}</span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: field.valueColor, overflowWrap: 'anywhere' }}>
            {field.value}
          </span>
          {field.canCopy ? (
            <button type="button" className="btn-g" onClick={() => copy(field.value, () => model.copied())} aria-label={field.aria} style={{ height: 24, fontSize: 'var(--t-small)' }}>
              Copy
            </button>
          ) : (
            <span />
          )}
        </div>
      ))}
      {face.agent.approval !== '' ? <p style={{ margin: '10px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>{face.agent.approval}</p> : null}
    </>
  )
}
