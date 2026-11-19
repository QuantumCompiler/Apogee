import { expect, test } from 'vitest'
import { SuitesViewModel } from '../../../source/presentation/suites/ViewModels/SuitesViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new SuitesViewModel().name).toBe('Suites')
})
