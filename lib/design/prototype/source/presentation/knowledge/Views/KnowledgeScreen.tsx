import type { RecordsViewModel } from '../ViewModels/RecordsViewModel'

/** The knowledge module's stub view: its name, nothing more. The empty app does not draw it. */
export function KnowledgeScreen({ model }: { model: RecordsViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
