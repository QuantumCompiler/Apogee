import deleteAndBackends from '@recorded/business/reads/Fixtures/delete-and-backends.txt?raw'
import deleteBackendClears from '@recorded/business/reads/Fixtures/delete-backend-clears.txt?raw'
import deleteBackendOlder from '@recorded/business/reads/Fixtures/delete-backend-older.txt?raw'
import deleteKept from '@recorded/business/reads/Fixtures/delete-kept.txt?raw'
import deletePlanGemma from '@recorded/business/reads/Fixtures/delete-plan-gemma.txt?raw'
import deletePlanNone from '@recorded/business/reads/Fixtures/delete-plan-none.txt?raw'
import { describe, expect, test } from 'vitest'
import { DeleteNarration, DeletePlan } from '../../../source/presentation/models/ViewModels/DeletePlan'

describe('what a delete says it would do, and what it said it did (the Mac’s DeletePlan and DeleteNarration)', () => {
  test('delete-plan-gemma.txt: the plan as printed, its closing re-run line left out (the confirm’s buttons are the yes), and the backend it names', () => {
    const plan = DeletePlan.of(deletePlanGemma)
    expect(plan.lines).toEqual([
      'will remove:',
      '  /Users/someone/.apogee/models/Gemma3-1B-Instruct/gguf/09f55af982ed',
      '',
      "backend 'Gemma3-1B-Instruct-Q4KM' points into this and will stop working until it is repointed.",
      'the backend entry stays unless you say -- --and-backends removes it with the weights.',
    ])
    expect(plan.backends).toEqual(['Gemma3-1B-Instruct-Q4KM'])
    expect(DeletePlan.question(plan, 'apogee models delete Gemma3-1B-Instruct/gguf/09f55af982ed --yes')).toBe(`${plan.lines.join('\n')}\n\napogee models delete Gemma3-1B-Instruct/gguf/09f55af982ed --yes -- this can't be undone.`)
  })

  test('delete-plan-none.txt names no backend, so no second button is offered', () => {
    expect(DeletePlan.of(deletePlanNone).backends).toEqual([])
  })

  test('what a delete printed is kept when it says more than what it removed; a removal that only removed says nothing more', () => {
    expect(DeleteNarration.said('Deleted x', deleteKept)?.lines.at(-1)).toMatch(/^kept backend 'Gemma3-1B-Instruct-Q4KM', which points at nothing now/)
    expect(DeleteNarration.said('Deleted x', deleteAndBackends)?.title).toBe('Deleted x')
    expect(DeleteNarration.said('Deleted x', 'will remove:\n  /a/b\n\nremoved.\n')).toBeUndefined()
  })

  test('a removal that cleared a role keeps its words; one that moved none says only its toast', () => {
    expect(DeleteNarration.movingRoles('Removed the backend Gemma4-12B-4bit', deleteBackendClears)?.lines).toEqual([
      "removed backend 'Gemma4-12B-4bit' from /Users/someone/.apogee/config/config.json",
      "models.default cleared: it named 'Gemma4-12B-4bit'",
    ])
    expect(DeleteNarration.movingRoles('Removed', deleteBackendOlder)).toBeUndefined()
  })
})
