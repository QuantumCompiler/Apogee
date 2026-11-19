import { expect, test } from 'vitest'
import { SessionLibraryViewModel } from '../../../source/presentation/sessions/ViewModels/SessionLibraryViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new SessionLibraryViewModel().name).toBe('Sessions')
})
