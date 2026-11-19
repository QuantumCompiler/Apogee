import { expect, test } from 'vitest'
import { VoiceViewModel } from '../../../source/presentation/voice/ViewModels/VoiceViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new VoiceViewModel().name).toBe('Voice')
})
