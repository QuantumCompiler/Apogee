import type { GraphViewModel } from '../ViewModels/GraphViewModel'

/** The graph module's stub view: its name, nothing more. The empty app does not draw it. */
export function GraphScreen({ model }: { model: GraphViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
