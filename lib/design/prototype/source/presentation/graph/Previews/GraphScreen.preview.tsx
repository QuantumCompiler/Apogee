import { GraphScreen } from '../Views/GraphScreen'
import { GraphViewModel } from '../ViewModels/GraphViewModel'

/** The stub's one state. */
export const Stub = () => <GraphScreen model={new GraphViewModel()} />
