import type { VideoViewModel } from '../ViewModels/VideoViewModel'

/** The video module's stub view: its name, nothing more. The empty app does not draw it. */
export function VideoScreen({ model }: { model: VideoViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
