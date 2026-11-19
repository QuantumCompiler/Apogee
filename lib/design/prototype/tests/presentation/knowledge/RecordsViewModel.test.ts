import { expect, test } from 'vitest'
import { RecordsViewModel } from '../../../source/presentation/knowledge/ViewModels/RecordsViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new RecordsViewModel().name).toBe('Knowledge')
})
