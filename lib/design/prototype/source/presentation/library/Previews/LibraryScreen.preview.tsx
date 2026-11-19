import { LibraryScreen } from '../Views/LibraryScreen'
import { AgentsViewModel } from '../ViewModels/AgentsViewModel'

/** The stub's one state. */
export const Stub = () => <LibraryScreen model={new AgentsViewModel()} />
