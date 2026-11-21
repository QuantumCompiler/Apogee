import { useObserved } from '../../styles/Observation/useObserved'
import type { TasksViewModel } from '../ViewModels/TasksViewModel'
import { TaskDetail } from './TaskDetail'
import { TaskList } from './TaskList'

/**
 * The Tasks place (design 1p, ⌘4): the old prototype's screen (`main.html`
 * at `b375fac9`, `ui.isTasks`) -- its header with New task…, the 300 px list
 * of tasks and the task's page -- drawn from the view-model's face. The New
 * task sheet and a refusal's alert are the shell's hosts', handed their
 * content by the composition.
 */
export function TasksScreen({ model }: { readonly model: TasksViewModel }) {
  useObserved(model)
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 14, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Tasks</h1>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>State a goal and what done looks like; watch the rounds and the checks</span>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="btn-p" onClick={() => void model.openNew()}>
          <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
            <path d="M8 3.5v9M3.5 8h9" />
          </svg>
          New task…
        </button>
      </header>
      <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
        <TaskList model={model} />
        <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '22px 28px 32px' }}>
          <TaskDetail model={model} />
        </div>
      </div>
    </div>
  )
}
