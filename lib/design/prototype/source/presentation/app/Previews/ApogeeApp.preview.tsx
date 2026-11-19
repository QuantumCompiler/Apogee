import { ApogeeApp } from '../Views/ApogeeApp'
import { AppViewModel } from '../ViewModels/AppViewModel'

/** The empty window, the app's one state until 1e. */
export const Empty = () => <ApogeeApp model={new AppViewModel()} />
