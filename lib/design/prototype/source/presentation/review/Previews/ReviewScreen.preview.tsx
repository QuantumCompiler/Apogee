import { ReviewScreen } from '../Views/ReviewScreen'
import { ReviewViewModel } from '../ViewModels/ReviewViewModel'

/** The stub's one state. */
export const Stub = () => <ReviewScreen model={new ReviewViewModel()} />
