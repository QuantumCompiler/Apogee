import { expect, test } from 'vitest'
import { TrainingViewModel } from '../../../source/presentation/training/ViewModels/TrainingViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new TrainingViewModel().name).toBe('Training')
})
