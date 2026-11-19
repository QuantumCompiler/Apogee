import type { ShellViewModel } from '../ViewModels/ShellViewModel'

/** The shell module's stub view: its name, nothing more. The empty app does not draw it. */
export function ShellView({ model }: { model: ShellViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
