import { expect, test } from 'vitest'
import { ConsoleViewModel } from '../../../source/presentation/console/ViewModels/ConsoleViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new ConsoleViewModel().name).toBe('Console')
})
