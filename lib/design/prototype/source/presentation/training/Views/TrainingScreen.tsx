import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'

/** The training module's stub view: its name, nothing more. The empty app does not draw it. */
export function TrainingScreen({ model }: { model: TrainingViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
