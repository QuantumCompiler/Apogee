import type { ReviewViewModel } from '../ViewModels/ReviewViewModel'

/** The review module's stub view: its name, nothing more. The empty app does not draw it. */
export function ReviewScreen({ model }: { model: ReviewViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
