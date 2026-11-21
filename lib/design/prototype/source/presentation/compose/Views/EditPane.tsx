import { useObserved } from '../../styles/Observation/useObserved'
import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'
import { Icon } from '../../styles/Components/Icon'

const label = { display: 'flex', flexDirection: 'column', gap: 6, color: 'var(--text-secondary)' } as const

/**
 * Make changes (design 1l, the old `comp.isProject` pane): the instruction,
 * the re-rolls, the new video's settings, Preview the changes -- its dry run,
 * with Stop -- and the preview's diff, notes and spend, whether Render plans
 * again, Save as, Render as a new video and Discard, and the command.
 */
export function EditPane({ model }: { readonly model: ComposeViewModel }) {
  useObserved(model)
  const face = model.face()
  const ed = face.edit
  const project = model.selectedProject
  return (
    <>
      <h2 style={{ margin: 0, fontSize: 'var(--t-h3)', fontWeight: 600 }}>Make changes</h2>
      <p style={{ margin: '4px 0 0', color: 'var(--text-tertiary)' }}>Changes render a new video. {face.page.file} and its project stay as they are.</p>
      {ed.locked ? <p style={{ margin: '12px 0 0', color: 'var(--status-warning)' }}>{ed.lockedText}</p> : null}
      <label style={{ ...label, marginTop: 14 }}>
        Edit by instruction
        <textarea rows={3} value={ed.text} onChange={(event) => model.editForm({ instruction: event.target.value })} disabled={ed.locked} placeholder="e.g. remove scene 5, or make the second scene show the indoor unit" style={{ boxSizing: 'border-box', width: '100%', resize: 'vertical', padding: '8px 10px', border: '1px solid var(--line-strong)', borderRadius: 6, background: 'var(--surface-card)', fontSize: 'var(--t-body)', lineHeight: 1.5, color: 'var(--text-primary)' }} />
      </label>
      <div style={{ marginTop: 6, color: 'var(--text-tertiary)' }}>{ed.textNote}</div>
      <div className="mono" style={{ marginTop: 10, fontSize: 'var(--t-meta)', color: ed.redo.color }}>
        {ed.redo.text}
      </div>
      <h3 className="lbl" style={{ margin: '16px 0 8px' }}>
        SETTINGS FOR THE NEW VIDEO
      </h3>
      <div style={{ display: 'grid', gridTemplateColumns: '70px minmax(0, 1fr)', gap: '8px 10px', alignItems: 'center' }}>
        <span style={{ color: 'var(--text-secondary)' }}>Voice</span>
        <select className="inp mono" value={ed.voice} onChange={(event) => model.editForm({ voice: event.target.value })} disabled={ed.locked} aria-label="Voice for the new video" style={{ width: '100%' }}>
          {ed.voices.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
        <span style={{ color: 'var(--text-secondary)' }}>Pipeline</span>
        <select className="inp" value={ed.pipeline} onChange={(event) => model.editForm({ model: event.target.value })} disabled={ed.locked} aria-label="Pipeline for the new video" style={{ width: '100%' }}>
          {ed.pipelines.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
        <span style={{ color: 'var(--text-secondary)' }}>Music</span>
        <select className="inp" value={ed.music} onChange={(event) => model.editForm({ music: event.target.value })} disabled={ed.locked} aria-label="Music bed for the new video" style={{ width: '100%' }}>
          {ed.musicChoices.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
        <span style={{ color: 'var(--text-secondary)' }}>Captions</span>
        <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8, cursor: 'pointer' }}>
          <input type="checkbox" checked={ed.captions} onChange={() => model.editForm({ captions: !ed.captions })} disabled={ed.locked} style={{ accentColor: 'var(--status-accent)' }} />
          {ed.captionsLabel}
        </label>
      </div>
      <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8, marginTop: 16 }}>
        {ed.previewing ? (
          <>
            <span className="mono" style={{ display: 'inline-flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-ui)', color: 'var(--text-secondary)' }}>
              <span className="pulse" style={{ width: 7, height: 7, borderRadius: '50%', background: 'var(--status-accent)' }} />
              {ed.previewingText}
            </span>
            <button type="button" className="btn-g" onClick={() => void model.stopPreview()} style={{ height: 24, fontSize: 'var(--t-small)' }}>
              Stop
            </button>
          </>
        ) : (
          <button type="button" className="btn" onClick={() => void model.previewEdit()} disabled={ed.cantPreview} title="Its dry run: the plan on paper, at most 5 minutes">
            Preview the changes
          </button>
        )}
        <span style={{ color: 'var(--text-tertiary)' }}>{ed.previewNote}</span>
      </div>
      {ed.failure === '' ? null : (
        <p role="alert" style={{ margin: '12px 0 0', color: 'var(--status-error)' }}>
          {ed.failure}
        </p>
      )}
      {ed.shown ? (
        <>
          <div className="card" style={{ marginTop: 12, padding: '12px 14px' }}>
            <div className="mono" style={{ fontSize: 'var(--t-ui)', fontWeight: 600 }}>
              {ed.head}
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 3, marginTop: 8 }}>
              {ed.diff.map((row) => (
                <div key={`${row.n}-${row.text}`} className="mono" style={{ display: 'grid', gridTemplateColumns: '22px minmax(0, 1fr)', gap: 6, fontSize: 'var(--t-ui)', color: row.color }}>
                  <span style={{ color: 'var(--text-tertiary)' }}>{row.n}</span>
                  <span>{row.text}</span>
                </div>
              ))}
            </div>
            {ed.notes.map((text) => (
              <div key={text} style={{ marginTop: 8, color: 'var(--text-secondary)' }}>
                {text}
              </div>
            ))}
            <div style={{ marginTop: 10, paddingTop: 10, borderTop: '1px solid var(--line-border)', color: 'var(--text-primary)' }}>{ed.spend}</div>
            <div style={{ marginTop: 4, color: 'var(--text-tertiary)' }}>Everything unchanged is copied byte for byte, with its record, under its new number.</div>
          </div>
          {ed.replans.text === '' ? null : (
            <p style={{ display: 'flex', alignItems: 'flex-start', gap: 6, margin: '10px 0 0', color: ed.replans.color }}>
              <Icon name="replan" size={13} style={{ flex: '0 0 auto', marginTop: 2 }} />
              <span>{ed.replans.text}</span>
            </p>
          )}
          <label style={{ ...label, marginTop: 12 }}>
            Save as
            <input type="text" className="inp mono" value={ed.out} onChange={(event) => model.editForm({ out: event.target.value })} style={{ width: '100%' }} />
          </label>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8, marginTop: 12 }}>
            <button type="button" className="btn-p" onClick={() => model.renderEdit()} title={ed.renderTitle}>
              Render as a new video
            </button>
            <button type="button" className="btn-g" onClick={() => project && model.edit(project)}>
              Discard
            </button>
          </div>
        </>
      ) : null}
      <h3 className="lbl" style={{ margin: '18px 0 8px' }}>
        ON THE COMMAND LINE
      </h3>
      <div className="mono" style={{ padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
        {ed.command}
      </div>
    </>
  )
}
