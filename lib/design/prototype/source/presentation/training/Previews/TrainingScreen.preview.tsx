import { TrainingScreen } from '../Views/TrainingScreen'
import { TrainingViewModel } from '../ViewModels/TrainingViewModel'

/** The stub's one state. */
export const Stub = () => <TrainingScreen model={new TrainingViewModel()} />
