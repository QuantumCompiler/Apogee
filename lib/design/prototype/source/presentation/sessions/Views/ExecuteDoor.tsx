import { useObserved } from '../../styles/Observation/useObserved'
import type { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'

const field = { display: 'flex', flexDirection: 'column', gap: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' } as const

/**
 * The execute door's content (design 1g): the old prototype's "New execute
 * session" (`ui.executeOpen`, `main.html:2351-2368` at `b375fac9`) -- the
 * suite and the model, the folder, Orchestrate, the note on what a project
 * brings, and the command exactly as it will run; its title and its Cancel
 * and Open are the shell's sheet host's. The Mac app's `ExecuteDoorSheet`,
 * its names: Orchestrate is off where no suite is named and none is the
 * default, as the Mac enforces it (`canOrchestrate`).
 */
export function ExecuteDoorSheet({ model }: { readonly model: SessionLibraryViewModel }) {
  useObserved(model)
  const door = model.door()
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: 12 }}>
        <label style={field}>
          Suite
          <select className="inp" value={door.suite} onChange={(event) => model.setExecuteForm({ suite: event.target.value === '' ? undefined : event.target.value })}>
            {door.suites.map((option) => (
              <option key={option.value} value={option.value}>
                {option.label}
              </option>
            ))}
          </select>
        </label>
        <label style={field}>
          Model
          <select className="inp" value={door.model} onChange={(event) => model.setExecuteForm({ model: event.target.value })}>
            {door.models.map((option) => (
              <option key={option.value} value={option.value}>
                {option.label}
              </option>
            ))}
          </select>
        </label>
      </div>
      <label style={field}>
        Folder
        <span style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <input type="text" className="inp mono" value={door.folder} readOnly aria-label="Folder" style={{ flex: '1 1 auto' }} />
          <button type="button" className="btn">
            Choose…
          </button>
        </span>
      </label>
      <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-small)', color: door.canOrchestrate ? 'var(--text-primary)' : 'var(--text-tertiary)' }}>
        <input type="checkbox" checked={door.orchestrate} disabled={!door.canOrchestrate} onChange={() => model.setExecuteForm({ orchestrate: !model.executeForm.orchestrate })} />
        Orchestrate: offer the suite’s symphonies to its chat model, as tools
      </label>
      <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{door.note}</p>
      <div className="mono" style={{ padding: '8px 10px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
        {door.command}
      </div>
    </div>
  )
}
