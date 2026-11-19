import { SuitesScreen } from '../Views/SuitesScreen'
import { SuitesViewModel } from '../ViewModels/SuitesViewModel'

/** The stub's one state. */
export const Stub = () => <SuitesScreen model={new SuitesViewModel()} />
