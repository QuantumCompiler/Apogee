import { SettingsView } from '../Views/SettingsView'
import { SettingsViewModel } from '../ViewModels/SettingsViewModel'

/** The stub's one state. */
export const Stub = () => <SettingsView model={new SettingsViewModel()} />
