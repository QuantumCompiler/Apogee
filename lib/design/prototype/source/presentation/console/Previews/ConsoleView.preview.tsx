import { ConsoleView } from '../Views/ConsoleView'
import { ConsoleViewModel } from '../ViewModels/ConsoleViewModel'

/** The stub's one state. */
export const Stub = () => <ConsoleView model={new ConsoleViewModel()} />
