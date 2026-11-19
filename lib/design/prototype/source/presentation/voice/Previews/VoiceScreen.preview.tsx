import { VoiceScreen } from '../Views/VoiceScreen'
import { VoiceViewModel } from '../ViewModels/VoiceViewModel'

/** The stub's one state. */
export const Stub = () => <VoiceScreen model={new VoiceViewModel()} />
