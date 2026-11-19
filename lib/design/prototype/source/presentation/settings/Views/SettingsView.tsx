import type { SettingsViewModel } from '../ViewModels/SettingsViewModel'

/** The settings module's stub view: its name, nothing more. The empty app does not draw it. */
export function SettingsView({ model }: { model: SettingsViewModel }) {
  return <section aria-label={model.name}>{model.name}</section>
}
