import { expect, test } from 'vitest'
import { styles } from '../../../source/presentation/styles/Styles'

test('the stub names its module, and nothing more', () => {
  expect(styles.name).toBe('Styles')
})
