import type { TaskTone } from '../ViewModels/TasksFaces'

/** A tone's color: tokens.json's, by the variable each draws with. */
export const toneColor: Readonly<Record<TaskTone, string>> = {
  accent: 'var(--status-accent)',
  warning: 'var(--status-warning)',
  error: 'var(--status-error)',
  plain: 'var(--text-secondary)',
  quiet: 'var(--text-tertiary)',
}
