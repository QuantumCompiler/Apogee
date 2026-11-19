import { expect, test } from 'vitest'
import { VideoViewModel } from '../../../source/presentation/video/ViewModels/VideoViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new VideoViewModel().name).toBe('Video')
})
