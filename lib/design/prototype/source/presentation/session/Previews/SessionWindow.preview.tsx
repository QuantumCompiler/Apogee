import { SessionWindow } from '../Views/SessionWindow'
import { SessionViewModel } from '../ViewModels/SessionViewModel'

/** The stub's one state. */
export const Stub = () => <SessionWindow model={new SessionViewModel()} />
