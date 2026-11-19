import type { TasksViewModel } from '../ViewModels/TasksViewModel'

/** The tasks module's stub view: its name, nothing more. The empty app does not draw it. */
export function TasksScreen({ model }: { model: TasksViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
