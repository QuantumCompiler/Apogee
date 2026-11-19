import { VideoScreen } from '../Views/VideoScreen'
import { VideoViewModel } from '../ViewModels/VideoViewModel'

/** The stub's one state. */
export const Stub = () => <VideoScreen model={new VideoViewModel()} />
