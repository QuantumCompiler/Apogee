import { useObserved } from '../../styles/Observation/useObserved'
import type { ReviewViewModel } from '../ViewModels/ReviewViewModel'
import { Icon } from '../../styles/Components/Icon'

const label = { display: 'flex', flexDirection: 'column', gap: 6, color: 'var(--text-secondary)' } as const
const control = { width: '100%', height: 30 } as const
const mono = { ...control, fontFamily: 'var(--font-mono)' } as const
const check = { display: 'flex', alignItems: 'flex-start', gap: 8, color: 'var(--text-primary)', cursor: 'pointer' } as const
const box = { margin: '2px 0 0', accentColor: 'var(--status-accent)' } as const

/**
 * The add-target sheet's content (the old `gs.kind === 'target'` sheet at
 * `b375fac9`; the Mac's `TargetForm`): its name, what it watches, the forge
 * and its host, the lenses pinned, the grant to post, drafts, the model, and
 * `apogee review targets add …` built live from them, the lenses sorted. The
 * shell's sheet host frames it, its title and buttons handed in by `app`.
 */
export function TargetSheet({ model }: { readonly model: ReviewViewModel }) {
  useObserved(model)
  const sheet = model.targetSheet()
  if (!sheet.shown) return null
  return (
    <>
      <label style={label}>
        Name
        <input type="text" className="inp" value={sheet.name} onChange={(event) => model.editTarget({ name: event.target.value })} aria-label="Name" style={control} />
      </label>
      <label style={label}>
        Watch
        <select className="inp" value={sheet.watch} onChange={(event) => model.editTarget({ watch: event.target.value === 'owner' ? 'owner' : 'repo' })} style={control}>
          <option value="repo">A repository</option>
          <option value="owner">An owner&apos;s repositories</option>
        </select>
      </label>
      {sheet.watch === 'repo' ? (
        <label style={label}>
          Repository
          <input type="text" className="inp" value={sheet.repo} onChange={(event) => model.editTarget({ repo: event.target.value })} placeholder="owner/repo or group/project" aria-label="Repository" style={mono} />
        </label>
      ) : (
        <label style={label}>
          Owner
          <input type="text" className="inp" value={sheet.owner} onChange={(event) => model.editTarget({ owner: event.target.value })} placeholder="acme" aria-label="Owner" style={mono} />
        </label>
      )}
      <label style={label}>
        Forge
        <select className="inp" value={sheet.forge} onChange={(event) => model.editTarget({ forge: event.target.value })} style={control}>
          <option value="github">GitHub</option>
          <option value="gitlab">GitLab</option>
        </select>
      </label>
      <label style={label}>
        Host
        <input type="text" className="inp" value={sheet.host} onChange={(event) => model.editTarget({ host: event.target.value })} placeholder="the forge's own" aria-label="Host" style={mono} />
      </label>
      <div>
        <button type="button" className="btn" onClick={() => model.toggleLensMenu()} aria-haspopup="menu" aria-expanded={sheet.lensOpen}>
          {sheet.lensesLabel}
          <Icon name="chevronDown" size={11} />
        </button>
        {sheet.lensOpen ? (
          <div className="menu" role="menu" aria-label="Lenses" style={{ marginTop: 6, maxWidth: 280, padding: 4 }}>
            {sheet.lenses.map((lens) => (
              <button
                key={lens.name}
                type="button"
                role="menuitemcheckbox"
                aria-checked={lens.on}
                className="hv"
                onClick={() => model.toggleLens(lens.name)}
                style={{ width: '100%', display: 'grid', gridTemplateColumns: '16px minmax(0, 1fr)', alignItems: 'center', gap: 6, height: 28, padding: '0 10px', border: 0, borderRadius: 5, background: 'transparent', color: 'var(--text-primary)', fontSize: 'var(--t-dense)', textAlign: 'left' }}
              >
                <span style={{ color: 'var(--status-accent)' }}>
                  {lens.on ? (
                    <Icon name="check" size={12} />
                  ) : null}
                </span>
                <span className="mono">{lens.name}</span>
              </button>
            ))}
          </div>
        ) : null}
      </div>
      <label style={check}>
        <input type="checkbox" checked={sheet.post} onChange={() => model.editTarget({ post: !sheet.post })} style={box} />
        <span>Post when swept -- with nobody at the terminal</span>
      </label>
      <label style={check}>
        <input type="checkbox" checked={sheet.drafts} onChange={() => model.editTarget({ drafts: !sheet.drafts })} style={box} />
        <span>Review drafts too</span>
      </label>
      <label style={label}>
        Model
        <input type="text" className="inp" value={sheet.model} onChange={(event) => model.editTarget({ model: event.target.value })} placeholder="the chat role" aria-label="Model" style={control} />
      </label>
      <div className="mono" style={{ padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
        {sheet.command}
      </div>
    </>
  )
}
