import { ModelsScreen } from '../Views/ModelsScreen'
import { ModelsViewModel } from '../ViewModels/ModelsViewModel'

/** The stub's one state. */
export const Stub = () => <ModelsScreen model={new ModelsViewModel()} />
