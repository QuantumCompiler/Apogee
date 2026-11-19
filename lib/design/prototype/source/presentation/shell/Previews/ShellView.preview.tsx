import { ShellView } from '../Views/ShellView'
import { ShellViewModel } from '../ViewModels/ShellViewModel'

/** The stub's one state. */
export const Stub = () => <ShellView model={new ShellViewModel()} />
