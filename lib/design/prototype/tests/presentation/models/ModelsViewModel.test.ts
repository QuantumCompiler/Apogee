import { expect, test } from 'vitest'
import { ModelsViewModel } from '../../../source/presentation/models/ViewModels/ModelsViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new ModelsViewModel().name).toBe('Models')
})
