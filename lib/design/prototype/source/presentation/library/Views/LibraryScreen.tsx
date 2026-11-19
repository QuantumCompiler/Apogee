import type { AgentsViewModel } from '../ViewModels/AgentsViewModel'

/** The library module's stub view: its name, nothing more. The empty app does not draw it. */
export function LibraryScreen({ model }: { model: AgentsViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
