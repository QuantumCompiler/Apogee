import { expect, test } from 'vitest'
import { ModelsViewModel } from '../../../source/presentation/models/ViewModels/ModelsViewModel'

test('the stub names its module, and nothing more', () => {
  expect(new ModelsViewModel().name).toBe('Models')
})

test('the two hooks the composition wires until 1h ports Models (design 1e): the store tab, and each time it comes into view', () => {
  const models = new ModelsViewModel()
  expect(models.tab).toBe('backends')
  models.showStore()
  expect(models.tab).toBe('store')
  models.appeared()
  models.appeared()
  expect(models.appearances).toBe(2)
})
