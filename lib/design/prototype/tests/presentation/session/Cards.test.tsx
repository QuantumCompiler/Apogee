import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import type { RowFace } from '../../../source/presentation/session/ViewModels/SessionViewModel'
import { PermissionCardView } from '../../../source/presentation/session/Views/PermissionCardView'
import { QuestionCardView } from '../../../source/presentation/session/Views/QuestionCardView'

type QuestionFace = Extract<RowFace, { kind: 'question' }>
type PermissionFace = Extract<RowFace, { kind: 'permission' }>

const two: QuestionFace = {
  id: 3,
  kind: 'question',
  label: 'Claude is asking',
  items: [
    { header: 'Scope', question: 'Per upload, per batch, or per host?', options: [{ label: 'Per upload', description: '' }, { label: 'Per batch', description: '' }], picked: '' },
    { header: 'Window', question: 'Over what window?', options: [{ label: 'Per minute', description: '' }, { label: 'Per hour', description: '' }], picked: '' },
  ],
  answered: [],
  closed: false,
}
const nothing = () => {}
const answerDisabled = (markup: string) => /<button type="submit"[^>]*disabled=""/.test(markup)

describe('the cards, drawn: what only the view decides (design 1v)', () => {
  test('a question of two items draws each, and Answer stays off until every item has an answer: one answer line per item', () => {
    const one = renderToStaticMarkup(<QuestionCardView card={two} answer={nothing} skip={nothing} picked={[1]} />)
    expect(one).toContain('Per upload, per batch, or per host?')
    expect(one).toContain('Over what window?')
    expect(one).toContain('WINDOW')
    expect(answerDisabled(one)).toBe(true)
    const both = renderToStaticMarkup(<QuestionCardView card={two} answer={nothing} skip={nothing} picked={[1, 0]} />)
    expect(answerDisabled(both)).toBe(false)
  })

  test("a preset's pick opens the card picked, Answer on; answered, it collapses to one line per item", () => {
    const preselected: QuestionFace = { ...two, items: [{ ...two.items[0], picked: 'Per batch' } as QuestionFace['items'][number]] }
    expect(answerDisabled(renderToStaticMarkup(<QuestionCardView card={preselected} answer={nothing} skip={nothing} />))).toBe(false)
    const answered = renderToStaticMarkup(<QuestionCardView card={{ ...two, answered: ['Per batch', 'Per minute'] }} answer={nothing} skip={nothing} />)
    expect(answered).toContain('asked: Per upload, per batch, or per host? → Per batch')
    expect(answered).toContain('asked: Over what window? → Per minute')
  })

  test('denied, the permission card draws the old note under its line; allowed, none', () => {
    const card: PermissionFace = { id: 2, kind: 'permission', title: 'Change a file?', subtitle: '', tool: 'write_file', target: 'src/ingest/runner.py', diff: '', added: 0, removed: 0, answered: 'denied by you', deniedNote: 'you denied write_file — root was told and carried on without it', closed: false }
    const denied = renderToStaticMarkup(<PermissionCardView card={card} answer={nothing} />)
    expect(denied).toContain('write_file src/ingest/runner.py — denied by you')
    expect(denied).toContain('you denied write_file — root was told and carried on without it')
    const allowed = renderToStaticMarkup(<PermissionCardView card={{ ...card, answered: 'allowed once', deniedNote: '' }} answer={nothing} />)
    expect(allowed).not.toContain('carried on without it')
  })
})
