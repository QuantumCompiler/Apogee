import { expect, test } from 'vitest'
import { GraphViewModel } from '../../../source/presentation/graph/ViewModels/GraphViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new GraphViewModel().name).toBe('Code graph')
})
