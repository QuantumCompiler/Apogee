import type { MonitorViewModel } from '../ViewModels/MonitorViewModel'

/** The monitor module's stub view: its name, nothing more. The empty app does not draw it. */
export function MonitorView({ model }: { model: MonitorViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
