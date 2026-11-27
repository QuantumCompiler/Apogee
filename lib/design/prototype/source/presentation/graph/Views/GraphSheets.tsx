import { useObserved } from '../../styles/Observation/useObserved'
import type { GraphViewModel } from '../ViewModels/GraphViewModel'

const command = { padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' } as const

/**
 * The save panel's body (design 1o): the old generic sheet's `savePanel`
 * kind (`main.html:8002` at `b375fac9`), the system's panel offering the
 * CLI's own file name -- where it goes, the name, and the command Save runs.
 * The shell's sheet host frames it, titled Save As, with Cancel and Save.
 */
export function SavePanel({ model }: { readonly model: GraphViewModel }) {
  useObserved(model)
  const panel = model.savePanel()
  if (!panel.shown) return null
  return (
    <>
      <p style={{ margin: 0, color: 'var(--text-tertiary)', overflowWrap: 'anywhere', whiteSpace: 'pre-wrap' }}>{panel.place}</p>
      <label style={{ display: 'flex', flexDirection: 'column', gap: 6, color: 'var(--text-secondary)' }}>
        <input type="text" className="inp" value={panel.file} onChange={(event) => model.setSaveFile(event.target.value)} aria-label="Save As" style={{ width: '100%', height: 30, fontFamily: 'var(--font-mono)' }} />
      </label>
      <div className="mono" style={command}>
        {panel.command}
      </div>
    </>
  )
}

/**
 * The dedupe sheet's body (design 1o): the old generic sheet's `dedupe` kind
 * (`main.html:8013` at `b375fac9`) and the Mac's `DedupeSheet` -- the dry
 * run as the CLI printed it, what Merge does where it names a group, the
 * merge supervised, then what it printed. The shell's sheet host frames it,
 * with Cancel and Merge them, or Done.
 */
export function DedupeSheet({ model }: { readonly model: GraphViewModel }) {
  useObserved(model)
  const sheet = model.dedupeSheet()
  if (!sheet.shown) return null
  return (
    <>
      {sheet.intro !== '' ? <p style={{ margin: 0, color: 'var(--text-secondary)', overflowWrap: 'anywhere', whiteSpace: 'pre-wrap' }}>{sheet.intro}</p> : null}
      {sheet.note !== '' ? <p style={{ margin: 0, color: 'var(--text-tertiary)', overflowWrap: 'anywhere', whiteSpace: 'pre-wrap' }}>{sheet.note}</p> : null}
      {sheet.printed !== '' ? (
        <div className="mono" style={{ ...command, maxHeight: 260, overflowY: 'auto', padding: '10px 12px' }}>
          {sheet.printed}
        </div>
      ) : null}
      {sheet.failure !== '' ? (
        <p role="alert" style={{ margin: 0, color: 'var(--status-error)', overflowWrap: 'anywhere', whiteSpace: 'pre-wrap' }}>
          {sheet.failure}
        </p>
      ) : null}
      {sheet.busy !== '' ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, color: 'var(--text-secondary)' }}>
          <span className="pulse" aria-hidden="true" style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--status-accent)' }} />
          <span className="mono" style={{ fontSize: 'var(--t-meta)', overflowWrap: 'anywhere' }}>
            {sheet.busy}
          </span>
        </div>
      ) : null}
    </>
  )
}
