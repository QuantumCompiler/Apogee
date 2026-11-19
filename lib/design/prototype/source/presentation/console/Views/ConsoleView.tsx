import type { ConsoleViewModel } from '../ViewModels/ConsoleViewModel'

/** The console module's stub view: its name, nothing more. The empty app does not draw it. */
export function ConsoleView({ model }: { model: ConsoleViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
