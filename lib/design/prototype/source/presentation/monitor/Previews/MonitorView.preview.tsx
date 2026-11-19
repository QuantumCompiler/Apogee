import { MonitorView } from '../Views/MonitorView'
import { MonitorViewModel } from '../ViewModels/MonitorViewModel'

/** The stub's one state. */
export const Stub = () => <MonitorView model={new MonitorViewModel()} />
