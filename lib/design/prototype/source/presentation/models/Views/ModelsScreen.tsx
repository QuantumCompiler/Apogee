import type { ModelsViewModel } from '../ViewModels/ModelsViewModel'

/** The models module's stub view: its name, nothing more. The empty app does not draw it. */
export function ModelsScreen({ model }: { model: ModelsViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
