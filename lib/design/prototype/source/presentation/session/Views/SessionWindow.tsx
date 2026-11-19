import type { SessionViewModel } from '../ViewModels/SessionViewModel'

/** The session module's stub view: its name, nothing more. The empty app does not draw it. */
export function SessionWindow({ model }: { model: SessionViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
