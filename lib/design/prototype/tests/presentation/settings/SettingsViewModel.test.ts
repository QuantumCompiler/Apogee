import { expect, test } from 'vitest'
import { SettingsViewModel } from '../../../source/presentation/settings/ViewModels/SettingsViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new SettingsViewModel().name).toBe('Settings')
})
