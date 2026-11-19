import { TasksScreen } from '../Views/TasksScreen'
import { TasksViewModel } from '../ViewModels/TasksViewModel'

/** The stub's one state. */
export const Stub = () => <TasksScreen model={new TasksViewModel()} />
