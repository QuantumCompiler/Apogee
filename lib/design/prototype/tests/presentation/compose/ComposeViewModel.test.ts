import { expect, test } from 'vitest'
import { ComposeViewModel } from '../../../source/presentation/compose/ViewModels/ComposeViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new ComposeViewModel().name).toBe('Compositions')
})
