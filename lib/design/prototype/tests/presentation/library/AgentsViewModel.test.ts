import { expect, test } from 'vitest'
import { AgentsViewModel } from '../../../source/presentation/library/ViewModels/AgentsViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new AgentsViewModel().name).toBe('Library')
})
