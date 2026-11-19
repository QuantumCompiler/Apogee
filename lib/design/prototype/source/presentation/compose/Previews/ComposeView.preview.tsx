import { ComposeView } from '../Views/ComposeView'
import { ComposeViewModel } from '../ViewModels/ComposeViewModel'

/** The stub's one state. */
export const Stub = () => <ComposeView model={new ComposeViewModel()} />
