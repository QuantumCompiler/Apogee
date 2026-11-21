import { useObserved } from '../../styles/Observation/useObserved'
import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'

/** The compositions list (design 1l, the old `comp.list`): the draft, each project newest first with its status, what the scan passed over, and how a composition is changed. */
export function ProjectList({ model }: { readonly model: ComposeViewModel }) {
  useObserved(model)
  const face = model.face()
  const row = { width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '9px 12px', marginBottom: 4, border: 0, borderRadius: 7, color: 'var(--text-primary)', textAlign: 'left' } as const
  return (
    <nav aria-label="Compositions" style={{ flex: '0 0 264px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', borderRight: '1px solid var(--line-border)' }}>
      {face.draft.shown ? (
        <button type="button" className="hv" onClick={() => model.select('new')} aria-current={face.draft.current} style={{ ...row, background: face.draft.current ? 'var(--surface-selection)' : '' }}>
          <span style={{ fontSize: 'var(--t-body)', fontWeight: 500 }}>New composition</span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            {face.draft.meta}
          </span>
        </button>
      ) : null}
      {face.list.map((project) => (
        <button key={project.output} type="button" className="hv" onClick={() => model.select(project.output)} aria-current={project.current} style={{ ...row, background: project.current ? 'var(--surface-selection)' : '' }}>
          <span style={{ display: 'flex', alignItems: 'center', gap: 8, width: '100%', minWidth: 0 }}>
            {project.rendering ? <span className="pulse" aria-label="rendering" style={{ flex: '0 0 auto', width: 6, height: 6, borderRadius: '50%', background: 'var(--status-accent)' }} /> : null}
            <span style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-body)', fontWeight: 500, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{project.title}</span>
            {project.tag === '' ? null : (
              <span className="mono" style={{ flex: '0 0 auto', padding: '0 5px', borderRadius: 3, background: 'var(--surface-control)', fontSize: 'var(--t-tag)', letterSpacing: '.06em', color: 'var(--text-secondary)' }}>
                {project.tag}
              </span>
            )}
          </span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
            {project.file}
          </span>
          <span style={{ fontSize: 'var(--t-small)', color: project.statusColor }}>{project.status}</span>
        </button>
      ))}
      {face.skipped.map((line) => (
        <p key={line} className="mono" style={{ margin: '6px 12px 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
          {line}
        </p>
      ))}
      <p style={{ margin: '10px 12px 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>A composition is edited by typing what to change. Each edit renders a new video; the one it came from stays as it was.</p>
    </nav>
  )
}
