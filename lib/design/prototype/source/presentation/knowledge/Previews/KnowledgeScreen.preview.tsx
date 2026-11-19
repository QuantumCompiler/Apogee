import { KnowledgeScreen } from '../Views/KnowledgeScreen'
import { RecordsViewModel } from '../ViewModels/RecordsViewModel'

/** The stub's one state. */
export const Stub = () => <KnowledgeScreen model={new RecordsViewModel()} />
