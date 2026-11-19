import { expect, test } from 'vitest'
import { ShellViewModel } from '../../../source/presentation/shell/ViewModels/ShellViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new ShellViewModel().name).toBe('Shell')
})
