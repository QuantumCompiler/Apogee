import { expect, test } from 'vitest'
import { TasksViewModel } from '../../../source/presentation/tasks/ViewModels/TasksViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new TasksViewModel().name).toBe('Tasks')
})
