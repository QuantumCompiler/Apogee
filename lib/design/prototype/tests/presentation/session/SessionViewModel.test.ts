import { expect, test } from 'vitest'
import { SessionViewModel } from '../../../source/presentation/session/ViewModels/SessionViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new SessionViewModel().name).toBe('Session')
})
