import { describe, expect, test } from 'vitest'
import { contractCalls, contractDecisions, contractGaps, contractParts, contractRows, startingUp } from '../../../source/presentation/gallery/Models/contract'
import type { ContractRow, Gap } from '../../../source/presentation/gallery/Models/contract'
import { BuildContractViewModel } from '../../../source/presentation/gallery/ViewModels/BuildContractViewModel'

const contract = (rows: readonly ContractRow[] = contractRows, gaps: readonly Gap[] = contractGaps) => new BuildContractViewModel(rows, gaps)
const counts = (model: BuildContractViewModel) => model.cards().map((card) => [card.status, card.count])

describe("the contract page's data (design 1r): BuildContract at 1c's frozen revision, as typed data", () => {
  test("the frozen board's 11 parts, 5 steps, 10 decisions and 3 calls", () => {
    expect(contractParts).toEqual(['SESSIONS', 'MODELS', 'SUITES', 'TASKS', 'CODE GRAPH', 'KNOWLEDGE', 'TRAINING', 'LIBRARY', 'REVIEW', 'VIDEO AND VOICE', 'SETTINGS AND SYSTEM'])
    expect(startingUp.map((step) => step.lead)).toEqual(['Find the binary.', 'Check the vocabulary.', 'Check health.', 'First reads.', 'Open the last session.'])
    expect(contractDecisions).toHaveLength(10)
    expect(contractCalls.map((call) => call.lead)).toEqual(['Which gaps land before the app’s first release?', 'The lowest macOS version', 'If the CLI isn’t installed'])
  })

  test("the board's 108 rows, then the 8 the shipped items handed it (1e's six, 1h's two), each at the end of its part", () => {
    expect(contractRows).toHaveLength(116)
    const handed = contractRows.filter((row) => row.status === 'gap' && Number(row.gap.slice(1)) > 24)
    expect(handed.map((row) => [row.part, row.status === 'gap' ? row.gap : ''])).toEqual([
      ['MODELS', 'G31'],
      ['MODELS', 'G32'],
      ['SETTINGS AND SYSTEM', 'G25'],
      ['SETTINGS AND SYSTEM', 'G26'],
      ['SETTINGS AND SYSTEM', 'G27'],
      ['SETTINGS AND SYSTEM', 'G28'],
      ['SETTINGS AND SYSTEM', 'G29'],
      ['SETTINGS AND SYSTEM', 'G30'],
    ])
    for (const row of handed) {
      const inPart = contractRows.filter((each) => each.part === row.part)
      const first = inPart.findIndex((each) => each.status === 'gap' && Number(each.gap.slice(1)) > 24)
      expect(inPart.slice(first).every((each) => each.status === 'gap' && Number(each.gap.slice(1)) > 24), row.part).toBe(true)
    }
  })

  test("the gaps: the board's G1 to G24, then G25 to G32 from the shipped items, each open one Later", () => {
    expect(contractGaps.map((gap) => gap.id)).toEqual(Array.from({ length: 32 }, (_, index) => `G${index + 1}`))
    const when = (ids: readonly Gap[]) => ids.reduce<Record<string, number>>((tally, gap) => ({ ...tally, [gap.when]: (tally[gap.when] ?? 0) + 1 }), {})
    expect(when(contractGaps.slice(0, 24))).toEqual({ Closed: 16, Later: 4, 'Before v1': 4 })
    expect(when(contractGaps.slice(24))).toEqual({ Later: 8 })
    for (const gap of contractGaps.slice(24)) expect(gap.when === 'Closed' ? '' : gap.fallback, gap.id).not.toBe('')
  })

  test('the words are plain text, inline code in backticks: no markup or entity is left of the board', () => {
    const leftover = /<\/?(?:span|b|div|br)\b|&[a-z]+;/
    for (const row of contractRows) expect(`${row.shows} ${row.how}`).not.toMatch(leftover)
    for (const gap of contractGaps) expect(`${gap.title} ${gap.change} ${gap.when === 'Closed' ? gap.note : gap.fallback}`).not.toMatch(leftover)
    for (const lead of [...startingUp, ...contractDecisions, ...contractCalls]) expect(`${lead.lead} ${lead.text}`).not.toMatch(leftover)
    expect(contractRows[2]?.how).toContain('Start again resuming with `--resume <id>`')
    expect(contractRows[1]?.how).toBe('send `{"type":"cancel"}`; the turn still ends in one `result`, `finish_reason: "cancelled"`')
  })
})

describe('the stat cards, counted (design 1r’s guardrail)', () => {
  test('the rows counted by status, and the gaps open by their when: 87 ready, 13 app, 16 gaps open, 0 assumed', () => {
    expect(counts(contract())).toEqual([
      ['ready', 87],
      ['app', 13],
      ['gap', 16],
      ['verify', 0],
    ])
    expect(contract().cards().map((card) => card.words)).toEqual([
      'parts drawn from what the CLI does today',
      'the app does itself, from what exists',
      'gaps open, each an additive CLI change',
      'assumed; each checked by probe',
    ])
  })

  test('a planted extra ready row raises the ready card by one, with no other edit', () => {
    const planted: ContractRow = { part: 'MODELS', shows: 'A planted part', how: '`models planted`', status: 'ready' }
    expect(counts(contract([...contractRows, planted]))).toEqual([
      ['ready', 88],
      ['app', 13],
      ['gap', 16],
      ['verify', 0],
    ])
  })

  test('a planted verify row raises the assumed card; a planted open gap, named by a row, raises the gaps card', () => {
    const verify: ContractRow = { part: 'TASKS', shows: 'A planted part', how: 'assumed', status: 'verify' }
    const gap: Gap = { id: 'G33', title: 'A planted gap', change: 'a planted change', when: 'Later', fallback: 'a planted fallback' }
    const naming: ContractRow = { part: 'TASKS', shows: 'Another planted part', how: 'none yet', status: 'gap', gap: 'G33' }
    expect(counts(contract([...contractRows, verify, naming], [...contractGaps, gap]))).toEqual([
      ['ready', 87],
      ['app', 13],
      ['gap', 17],
      ['verify', 1],
    ])
  })

  test('the gaps card counts the gaps open, not the rows naming them: a second row naming G25 leaves it at 16', () => {
    const second: ContractRow = { part: 'MODELS', shows: 'A second part needing G25', how: 'none yet', status: 'gap', gap: 'G25' }
    expect(counts(contract([...contractRows, second]))).toEqual([
      ['ready', 87],
      ['app', 13],
      ['gap', 16],
      ['verify', 0],
    ])
  })
})

describe('the contract map (design 1r)', () => {
  test("the rows grouped by part, in the parts' order, each part's rows in table order", () => {
    const parts = contract().parts()
    expect(parts.map((part) => part.part)).toEqual([...contractParts])
    // The frozen board's 24, 17, 2, 9, 6, 3, 10, 4, 7, 16 and 10, with Models' two and Settings and system's six handed in.
    expect(parts.map((part) => part.rows.length)).toEqual([24, 19, 2, 9, 6, 3, 10, 4, 7, 16, 16])
    expect(parts.flatMap((part) => part.rows.map((row) => row.shows))).toEqual(contractRows.map((row) => row.shows))
  })

  test("each row's chip: its status in the board's words and tone, and a gap chip linked to its gap", () => {
    const rows = contract().parts().flatMap((part) => part.rows)
    const chip = (shows: string) => rows.find((row) => row.shows === shows)?.chip
    expect(chip('Stop')).toEqual({ label: 'ready', tone: 'status.accent', dashed: false })
    expect(chip('Slash commands')).toEqual({ label: 'app', tone: 'text.secondary', dashed: false })
    expect(chip('Register every set that isn’t yet')).toEqual({ label: 'gap G19', tone: 'status.warning', dashed: false, gap: 'G19', anchor: 'contract-gap-G19' })
    expect(contract().gapRows().find((gap) => gap.id === 'G19')?.anchor).toBe('contract-gap-G19')
  })

  test('a verify chip is dashed, in the tertiary text', () => {
    const verify: ContractRow = { part: 'TASKS', shows: 'A planted part', how: 'assumed', status: 'verify' }
    const rows = contract([...contractRows, verify]).parts().flatMap((part) => part.rows)
    expect(rows.find((row) => row.shows === 'A planted part')?.chip).toEqual({ label: 'verify', tone: 'text.tertiary', dashed: true })
  })

  test('the gaps: a closed one dimmed, its note after "Closed:", in the accent; an open one its fallback after "Until then:"', () => {
    const gaps = contract().gapRows()
    expect(gaps).toHaveLength(32)
    expect(gaps[0]).toMatchObject({ id: 'G1', closed: true, until: 'Closed: it shipped with the CLI’s 52a, and the app draws it: the card’s line counts and Show changes, from the diff; checked by probe.', when: 'Closed' })
    expect(gaps.find((gap) => gap.id === 'G20')).toMatchObject({ closed: false, until: 'Until then: Report pass or fail from the exit status, with the CLI’s text in the job’s log.', when: 'Before v1' })
  })
})

describe('the gaps against the rows (design 1r’s guardrail)', () => {
  test('every gap row names a gap that exists and is open, and every open gap is named by a row', () => {
    expect(contract().problems()).toEqual([])
  })

  test('a planted gap G99 fails, naming its row', () => {
    const planted: ContractRow = { part: 'REVIEW', shows: 'A planted part', how: 'nothing yet', status: 'gap', gap: 'G99' }
    expect(contract([...contractRows, planted]).problems()).toEqual(['the row "A planted part" names gap G99, which no gap is'])
  })

  test('a row naming a closed gap fails, naming both', () => {
    const planted: ContractRow = { part: 'REVIEW', shows: 'A planted part', how: 'nothing yet', status: 'gap', gap: 'G1' }
    expect(contract([...contractRows, planted]).problems()).toEqual(['the row "A planted part" names gap G1, which is closed'])
  })

  test('a planted open gap no row names fails, naming it', () => {
    const planted: Gap = { id: 'G33', title: 'A planted gap', change: 'a planted change', when: 'Before v1', fallback: 'a planted fallback' }
    expect(contract(contractRows, [...contractGaps, planted]).problems()).toEqual(['gap G33 is open, and no row names it'])
  })
})
