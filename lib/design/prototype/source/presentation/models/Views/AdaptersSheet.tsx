import { useObserved } from '../../styles/Observation/useObserved'
import type { ModelsViewModel } from '../ViewModels/ModelsViewModel'

/**
 * The adapters sheet's content (design 1h): the old prototype's generic
 * sheet of kind `adapters` (`main.html` at `b375fac9`, section `sheets`) --
 * the list as rows of path and scale, Add a file…, the command as typed --
 * the Mac app's `AdaptersSheet`. The frame and its buttons are the shell's.
 */
export function AdaptersSheet({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  const sheet = model.face().adaptersSheet
  return (
    <>
      <p style={{ margin: 0, color: 'var(--text-secondary)' }}>{sheet.intro}</p>
      {sheet.none !== '' ? <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{sheet.none}</p> : null}
      {sheet.rows.length > 0 ? (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
          {sheet.rows.map((row, index) => (
            <div key={index} style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <input type="text" className="inp mono" value={row.path} onChange={(event) => model.editAdaptersForm((form) => form.edit(index, { path: event.target.value }))} placeholder="Path" aria-label="Path" style={{ flex: '1 1 auto', minWidth: 0 }} />
              {sheet.scaled ? <input type="text" className="inp mono" value={row.scale} onChange={(event) => model.editAdaptersForm((form) => form.edit(index, { scale: event.target.value }))} placeholder="1" aria-label="Scale" style={{ width: 60 }} /> : null}
              <button type="button" className="ib" onClick={() => model.editAdaptersForm((form) => form.remove(index))} aria-label="Take it off" title="Take it off" style={{ width: 26, height: 26 }}>
                <svg className="i" width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
                  <circle cx="8" cy="8" r="5.5" />
                  <path d="M5.5 8h5" />
                </svg>
              </button>
            </div>
          ))}
        </div>
      ) : null}
      <div>
        <button type="button" className="btn-g" onClick={() => model.editAdaptersForm((form) => form.add())} disabled={!sheet.canAdd}>
          {sheet.addLabel}
        </button>
      </div>
      <div className="mono" style={{ padding: '8px 10px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
        {sheet.command}
      </div>
    </>
  )
}
