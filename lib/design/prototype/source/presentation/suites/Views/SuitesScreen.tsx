import type { SuitesViewModel } from '../ViewModels/SuitesViewModel'

/** The suites module's stub view: its name, nothing more. The empty app does not draw it. */
export function SuitesScreen({ model }: { model: SuitesViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
