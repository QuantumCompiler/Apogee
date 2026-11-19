import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'

/** The compose module's stub view: its name, nothing more. The empty app does not draw it. */
export function ComposeView({ model }: { model: ComposeViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
