import { useObserved } from '../../styles/Observation/useObserved'
import type { TasksViewModel } from '../ViewModels/TasksViewModel'
import { toneColor } from './TaskTones'

/** The 300 px list: each task's goal and its status by its dot; past the newest 50, the count and Show all. */
export function TaskList({ model }: { readonly model: TasksViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <nav aria-label="Tasks" style={{ flex: '0 0 300px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', borderRight: '1px solid var(--line-border)' }}>
      {face.list.map((row) => (
        <button
          key={row.id}
          type="button"
          className="hv"
          onClick={() => void model.select(row.id)}
          aria-current={row.current}
          style={{ width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '10px 12px', marginBottom: 4, border: 0, borderRadius: 7, background: row.current ? 'var(--surface-selection)' : '', color: 'var(--text-primary)', textAlign: 'left' }}
        >
          <span style={{ fontSize: 'var(--t-body)', fontWeight: 500 }}>{row.goal}</span>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)', color: toneColor[row.tone] }}>
            <span style={{ width: 6, height: 6, borderRadius: '50%', background: toneColor[row.tone] }} />
            {row.status}
          </span>
        </button>
      ))}
      {face.truncation !== '' ? (
        <p style={{ margin: '8px 12px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>
          {face.truncation} ·{' '}
          <button
            type="button"
            onClick={() => void model.showAll()}
            title="apogee task list --all"
            style={{ padding: 0, border: 0, background: 'transparent', color: 'var(--status-accent)', fontSize: 'var(--t-small)', textDecoration: 'underline', textUnderlineOffset: 3 }}
          >
            Show all
          </button>
        </p>
      ) : null}
      {face.empty !== '' ? <p style={{ margin: '6px 12px', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{face.empty}</p> : null}
    </nav>
  )
}
