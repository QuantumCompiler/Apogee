import { describe, expect, test } from 'vitest'
import { recordedRawRules, recordsQuestion, rejectedRecord } from '../../../source/presentation/knowledge/Previews/KnowledgeWorlds'
import { callsSince, knowledgePlace, settle } from './KnowledgeHarness'

async function listed(options: Parameters<typeof knowledgePlace>[0] = {}) {
  const place = knowledgePlace(options)
  await place.records.appeared()
  return place
}

describe("the decision records (design 1o): the Mac app's RecordsViewModel over knowledge-list.json and knowledge-query.json, drawn as the old Records tab draws them", () => {
  test('knowledge list: the three records in the CLI’s order, each its decision, no score, and its branch, discipline and day; none chosen', async () => {
    const { records, child } = await listed()
    expect(callsSince(child, 0)).toEqual(['reads knowledge list'])
    const face = records.face()
    expect(face.rows.map((row) => [row.decision, row.score, row.meta, row.current])).toEqual([
      ['Lead release notes with user-felt changes; process last.', '', 'shipped · product · 2026-10-21', false],
      ["Default the window to the model's trained length, capped by free memory.", '', 'shipped · eng · 2026-10-21', false],
      ['Remove the cancel button from checkout.', '', 'rejected · ux · 2026-10-21', false],
    ])
    expect([face.found, face.empty, face.record.shown, face.noSelection]).toEqual(['', '', false, 'Choose a record to read its reasoning and its decision.'])
  })

  test('asking orders the records by knowledge-query.json’s scores, said to three places (0.596, 0.000, 0.000), with what was found and how', async () => {
    const { records, child } = await listed()
    records.setQuestion(recordsQuestion)
    const from = child.calls.length
    await records.ask()
    expect(callsSince(child, from)).toEqual([`commands knowledge query --status  --json -- ${recordsQuestion}`])
    const face = records.face()
    expect([face.question, face.found, face.failure]).toEqual([recordsQuestion, '3 found · lexical', ''])
    expect(face.rows.map((row) => [row.decision, row.score])).toEqual([
      ['Remove the cancel button from checkout.', '0.596'],
      ["Default the window to the model's trained length, capped by free memory.", '0.000'],
      ['Lead release notes with user-felt changes; process last.', '0.000'],
    ])
  })

  test('Ask with nothing typed asks the old question, filling the field, as the old kno.ask does; under a branch it asks with that branch, which nobody recorded, said in place', async () => {
    const { records, child } = await listed()
    let from = child.calls.length
    await records.ask()
    expect(callsSince(child, from)).toEqual([`commands knowledge query --status  --json -- ${recordsQuestion}`])
    let face = records.face()
    expect([face.question, face.found, face.failure, face.rows.map((row) => row.score)]).toEqual([recordsQuestion, '3 found · lexical', '', ['0.596', '0.000', '0.000']])
    records.setBranch('rejected')
    expect(records.face().question, 'a branch picked clears it, as the old onBranch does').toBe('')
    from = child.calls.length
    await records.ask()
    expect(callsSince(child, from)).toEqual([`commands knowledge query --status rejected --json -- ${recordsQuestion}`])
    face = records.face()
    expect([face.question, face.found, face.failure]).toEqual([recordsQuestion, '', `no recorded answer for apogee knowledge query --status rejected --json -- ${recordsQuestion}`])
    records.setBranch('')
    records.setQuestion('   ')
    await records.ask()
    face = records.face()
    expect([face.question, face.found, face.failure], 'blank is nothing typed').toEqual([recordsQuestion, '3 found · lexical', ''])
  })

  test('a question nobody recorded says the stand-in’s line where the found line stands, and the list stays', async () => {
    const { records } = await listed()
    records.setQuestion('what did we ship')
    await records.ask()
    const face = records.face()
    expect([face.found, face.failure]).toEqual(['', 'no recorded answer for apogee knowledge query --status  --json -- what did we ship'])
    expect(face.rows.map((row) => row.score)).toEqual(['', '', ''])
  })

  test('a reply still coming when a branch is picked, or the question cleared, never lands', async () => {
    const { records } = await listed()
    records.setQuestion(recordsQuestion)
    const picked = records.ask()
    records.setBranch('rejected')
    await picked
    let face = records.face()
    expect([face.question, face.found, face.failure, face.rows.map((row) => row.score)]).toEqual(['', '', '', ['']])
    records.setBranch('')
    records.setQuestion(recordsQuestion)
    const cleared = records.ask()
    records.clearQuestion()
    await cleared
    face = records.face()
    expect([face.question, face.found, face.rows.map((row) => row.score)]).toEqual(['', '', ['', '', '']])
  })

  test('the branch filter shows Rejected alone, Superseded none and says so, and clears a standing question as the old one does', async () => {
    const { records } = await listed()
    records.setQuestion(recordsQuestion)
    await records.ask()
    records.setBranch('rejected')
    let face = records.face()
    expect([face.branch, face.question, face.found]).toEqual(['rejected', '', ''])
    expect(face.rows.map((row) => row.decision)).toEqual(['Remove the cancel button from checkout.'])
    records.setBranch('superseded')
    face = records.face()
    expect([face.rows, face.empty]).toEqual([[], 'No superseded records.'])
    records.setBranch('')
    expect(records.face().rows.length).toBe(3)
    expect(records.branches).toEqual([
      ['', 'Every branch'],
      ['shipped', 'Shipped'],
      ['rejected', 'Rejected'],
      ['superseded', 'Superseded'],
    ])
  })

  test('a record open: its decision and id, then WHY, BRANCH, DISCIPLINE, FROM, LINKED TO and CAPTURED in the CLI’s words, the row current', async () => {
    const { records } = await listed()
    records.select(rejectedRecord)
    const face = records.face()
    expect(face.rows.map((row) => row.current)).toEqual([false, false, true])
    expect(face.record).toMatchObject({
      shown: true,
      id: 'kr-20261021T045944Z-1e2354',
      decision: 'Remove the cancel button from checkout.',
      facts: [
        { label: 'WHY', value: 'We dropped the cancel button because testers kept mistaking it for back.' },
        { label: 'BRANCH', value: 'rejected' },
        { label: 'DISCIPLINE', value: 'ux' },
        { label: 'FROM', value: 'meeting · Ada Lovelace' },
        { label: 'LINKED TO', value: 'nothing yet' },
        { label: 'CAPTURED', value: '2026-10-21T04:59:44.463192Z' },
      ],
    })
    records.select('kr-20261021T045944Z-3346a9')
    expect(records.face().record.facts.find((fact) => fact.label === 'LINKED TO')?.value).toBe('PROJ-42')
  })

  test('the raw toggle: offered where the record has an archive, its command named; Show reads knowledge info --raw and shows the recorded text; Hide closes it', async () => {
    const { records, child } = await listed()
    records.select(rejectedRecord)
    expect(records.face().record.raw).toEqual({ shown: true, open: false, text: '', note: '', failure: '', command: `apogee knowledge info ${rejectedRecord} --raw` })
    const from = child.calls.length
    await records.showRaw()
    expect(callsSince(child, from)).toEqual([`commands knowledge info ${rejectedRecord} --raw`])
    expect(records.face().record.raw).toMatchObject({ open: true, text: 'Ada: testers keep hitting cancel when they mean back. Grace: then we drop the cancel button from checkout. Ada: agreed, ship it.', failure: '' })
    records.hideRaw()
    expect(records.face().record.raw.open).toBe(false)
    records.select('kr-20261021T045954Z-0084c5')
    await records.showRaw()
    expect(records.face().record.raw.failure, 'a record whose conversation nobody recorded: the stand-in’s line, in place').toBe('no recorded answer for apogee knowledge info kr-20261021T045954Z-0084c5 --raw')
    records.select(rejectedRecord)
    expect(records.face().record.raw.open, 'another record chosen closes the one shown').toBe(false)
  })

  test('the one recorded raw conversation, of another record: knowledge-list-one.json and knowledge-info-raw.txt as recorded', async () => {
    const { records } = await listed({ rules: recordedRawRules() })
    records.select('kr-20261026T142411Z-2e8272')
    await records.showRaw()
    expect(records.face().record.raw.text).toBe('Ada: testers keep hitting cancel when they mean back. Grace: then we drop the cancel button from checkout. Ada: agreed, ship it.')
  })

  test('the five record actions stay the old toasts, word for word, and run nothing', async () => {
    const { records, child, said } = await listed()
    records.select(rejectedRecord)
    const from = child.calls.length
    records.capture()
    records.exportRecords()
    records.link()
    records.move()
    records.remove()
    expect(said).toEqual([
      'apogee knowledge capture: the clerk distils the conversation (prototype)',
      'apogee knowledge export --format markdown -o knowledge.md (prototype)',
      'apogee knowledge link (prototype)',
      'apogee knowledge status (prototype)',
      'apogee knowledge delete: confirmed first (prototype)',
    ])
    expect(callsSince(child, from)).toEqual([])
  })

  test('no apogee: no records and no words, as the old has no missing state (the Mac’s line a follow-up’s); a config typo: the CLI’s refusal, said where the list stands', async () => {
    const missing = await listed({ apogee: 'missing' })
    expect([missing.records.face().rows, missing.records.face().empty]).toEqual([[], ''])
    const typo = await listed({ apogee: 'config-typo' })
    expect(typo.records.face().empty).toBe("The records could not be read: apogee knowledge: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
    await settle()
  })
})
