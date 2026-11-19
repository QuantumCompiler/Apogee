import { expect, test } from 'vitest'
import { AppViewModel } from '../../../source/presentation/app/ViewModels/AppViewModel'

test('the empty app is named Apogee, and holds nothing more yet', () => {
  expect(new AppViewModel().name).toBe('Apogee')
})
