import { expect, test } from 'vitest'
import { MonitorViewModel } from '../../../source/presentation/monitor/ViewModels/MonitorViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new MonitorViewModel().name).toBe('Monitor')
})
