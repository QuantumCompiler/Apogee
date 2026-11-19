import { SessionsScreen } from '../Views/SessionsScreen'
import { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'

/** The stub's one state. */
export const Stub = () => <SessionsScreen model={new SessionLibraryViewModel()} />
