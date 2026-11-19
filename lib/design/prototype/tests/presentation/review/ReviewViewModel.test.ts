import { expect, test } from 'vitest'
import { ReviewViewModel } from '../../../source/presentation/review/ViewModels/ReviewViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new ReviewViewModel().name).toBe('Review')
})
