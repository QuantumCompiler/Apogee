import symphoniesList from '@recorded/business/reads/Fixtures/symphonies-list.json?raw'
import { expect, test } from 'vitest'
import { Symphony } from '../../../source/business/reads/Symphonies'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

test("symphonies list: each symphony's name and description, in the list's order (the Mac app's Symphony.list)", () => {
  const symphonies = Symphony.list(ReadDocument.from(symphoniesList) as ReadDocument)
  expect(symphonies.map((symphony) => symphony.name)).toEqual([
    'critique-revise', 'describe-answer', 'digest', 'extract-actions', 'extract-facts', 'redact-text', 'summarize-verify', 'quick-check', 'notes-digest',
  ])
  expect(symphonies[6]?.description).toBe('Summarize a passage, then check the summary against the passage and correct it.')
  expect(Symphony.list(ReadDocument.from('{"object":"list","data":[{"description":"no name"}]}') as ReadDocument)).toEqual([])
})
