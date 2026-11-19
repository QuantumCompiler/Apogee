import type { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'

/** The sessions module's stub view: its name, nothing more. The empty app does not draw it. */
export function SessionsScreen({ model }: { model: SessionLibraryViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
