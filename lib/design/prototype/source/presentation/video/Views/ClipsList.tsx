import { InstallCard } from '../../styles/Components/InstallCard'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VideoViewModel } from '../ViewModels/VideoViewModel'

/** The clips list (design 1k, the old `vid.list`): the install card, the run going now, then each clip newest first, and what the library passed over. */
export function ClipsList({ model }: { readonly model: VideoViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <nav aria-label="Clips" style={{ flex: '0 0 264px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', borderRight: '1px solid var(--line-border)' }}>
      {face.install.shown ? <InstallCard install={face.install} act={() => model.installAct()} /> : null}
      {face.list.map((row) => (
        <button key={row.id} type="button" className="hv" onClick={() => model.select(row.id)} aria-current={row.current} style={{ width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '9px 12px', marginBottom: 4, border: 0, borderRadius: 7, background: row.current ? 'var(--surface-selection)' : '', color: 'var(--text-primary)', textAlign: 'left' }}>
          <span style={{ display: 'flex', alignItems: 'center', gap: 8, width: '100%', minWidth: 0 }}>
            {row.running ? <span className="pulse" aria-label="generating" style={{ flex: '0 0 auto', width: 6, height: 6, borderRadius: '50%', background: 'var(--status-accent)' }} /> : null}
            <span className="mono" style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-ui)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
              {row.file}
            </span>
            <span className="mono" style={{ flex: '0 0 auto', padding: '0 5px', borderRadius: 3, background: 'var(--surface-control)', fontSize: 'var(--t-tag)', letterSpacing: '.06em', color: row.tagColor }}>
              {row.tag}
            </span>
          </span>
          <span style={{ width: '100%', fontSize: 'var(--t-small)', color: 'var(--text-secondary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{row.prompt}</span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: row.metaColor }}>
            {row.meta}
          </span>
        </button>
      ))}
      {face.empty === '' ? null : <p style={{ margin: '10px 12px 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{face.empty}</p>}
      {face.skipped.map((line) => (
        <p key={line} className="mono" style={{ margin: '8px 12px 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
          {line}
        </p>
      ))}
      <p style={{ margin: '10px 12px 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Every clip keeps its record beside it, so any run can be run again, finished at full quality or continued.</p>
    </nav>
  )
}
