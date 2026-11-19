import type { VoiceViewModel } from '../ViewModels/VoiceViewModel'

/** The voice module's stub view: its name, nothing more. The empty app does not draw it. */
export function VoiceScreen({ model }: { model: VoiceViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
