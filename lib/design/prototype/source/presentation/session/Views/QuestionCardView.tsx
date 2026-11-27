import { useState } from 'react'
import type { RowFace } from '../ViewModels/SessionViewModel'
import { QuietLine } from './TranscriptRow'

type QuestionFace = Extract<RowFace, { kind: 'question' }>

/**
 * The model asks (design 1v): each question it asks with its options, room
 * for an answer of one's own every time -- the options are a convenience,
 * not a constraint -- then Answer and Skip, as the old prototype's card draws
 * them (`main.html:550-562` at `b375fac9`). A question of several items takes
 * one answer for each, in order, so Answer is on once every item has one. The
 * choice changes freely until Answer is pressed; once written, an answer
 * cannot be taken back (the CLI reads one per question), so the card
 * collapses to what was asked and said, with no "change answer". Closed by
 * its turn's end, it says it went unanswered. The Mac app's
 * `QuestionCardView`.
 */
export function QuestionCardView({ card, answer, skip, picked: initial }: {
  readonly card: QuestionFace
  readonly answer: (answers: readonly string[]) => void
  readonly skip: () => void
  /** The options picked as it opens, one per item: a preview's; else the card's own (a preset's pick). */
  readonly picked?: ReadonlyArray<number | 'own' | undefined>
}) {
  const [picked, setPicked] = useState<ReadonlyArray<number | 'own' | undefined>>(
    () =>
      initial ??
      card.items.map((item) => {
        const at = item.options.findIndex((option) => option.label === item.picked)
        return at >= 0 ? at : undefined
      }),
  )
  const [own, setOwn] = useState<readonly string[]>(() => card.items.map(() => ''))
  if (card.answered.length > 0) return <>{card.items.map((item, index) => <QuietLine key={index} text={`asked: ${item.question} → ${card.answered[index] ?? ''}`} />)}</>
  if (card.closed) return <>{card.items.map((item, index) => <QuietLine key={index} text={`asked: ${item.question} → not answered: the turn ended first`} />)}</>
  const chosen = card.items.map((item, index) => {
    const pick = picked[index]
    return pick === 'own' ? (own[index] ?? '').trim() : pick === undefined ? undefined : item.options[pick]?.label
  })
  const ready = chosen.length > 0 && chosen.every((said) => said !== undefined && said !== '')
  const pick = (index: number, choice: number | 'own') => setPicked(card.items.map((_, at) => (at === index ? choice : picked[at])))
  const bordered = (on: boolean) => ({ border: `1px solid ${on ? 'var(--line-strong)' : 'var(--line-border)'}`, background: on ? 'var(--surface-selection)' : undefined })
  const headerTag = (header: string) =>
    header !== '' ? (
      <span className="mono" style={{ display: 'inline-flex', alignItems: 'center', height: 20, padding: '0 7px', border: '1px solid var(--line-strong)', borderRadius: 4, fontSize: 'var(--t-tag)', letterSpacing: '.08em', color: 'var(--text-secondary)' }}>
        {header.toUpperCase()}
      </span>
    ) : null
  return (
    <form
      aria-label={card.label.replace(/ is asking$/, '') === card.label ? 'Question' : `Question from ${card.label.replace(/ is asking$/, '')}`}
      onSubmit={(event) => {
        event.preventDefault()
        if (ready) answer(chosen.map((said) => said ?? ''))
      }}
      style={{ border: '1px solid var(--line-strong)', borderRadius: 8, background: 'var(--surface-card)', overflow: 'hidden' }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '12px 14px 4px' }}>
        {headerTag(card.items[0]?.header ?? '')}
        <span style={{ marginLeft: 'auto', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{card.label}</span>
      </div>
      {card.items.map((item, index) => (
        <div key={index}>
          {index > 0 && item.header !== '' ? <div style={{ padding: '12px 14px 0' }}>{headerTag(item.header)}</div> : null}
          <fieldset style={{ margin: 0, padding: '4px 14px 0', border: 0 }}>
          <legend style={{ padding: '8px 0 10px', fontSize: 'var(--t-prose)', fontWeight: 600 }}>{item.question}</legend>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            {item.options.map((option, at) => (
              <label key={option.label} style={{ display: 'grid', gridTemplateColumns: '18px minmax(0, 1fr) auto', columnGap: 10, padding: '9px 12px', borderRadius: 6, cursor: 'pointer', ...bordered(picked[index] === at) }}>
                <input type="radio" name={`question-${card.id}-${index}`} checked={picked[index] === at} onChange={() => pick(index, at)} style={{ margin: '3px 0 0', accentColor: 'var(--status-accent)' }} />
                <span>
                  <span style={{ display: 'block', fontSize: 'var(--t-body)', fontWeight: 500 }}>{option.label}</span>
                  {option.description !== '' ? <span style={{ display: 'block', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{option.description}</span> : null}
                </span>
                <span className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
                  {at + 1}
                </span>
              </label>
            ))}
            <label style={{ display: 'grid', gridTemplateColumns: '18px minmax(0, 1fr)', columnGap: 10, alignItems: 'center', padding: '7px 12px', borderRadius: 6, ...bordered(picked[index] === 'own') }}>
              <input type="radio" name={`question-${card.id}-${index}`} checked={picked[index] === 'own'} onChange={() => pick(index, 'own')} style={{ margin: 0, accentColor: 'var(--status-accent)' }} />
              <input
                type="text"
                value={own[index] ?? ''}
                onChange={(event) => {
                  setOwn(card.items.map((_, at) => (at === index ? event.target.value : (own[at] ?? ''))))
                  pick(index, 'own')
                }}
                onFocus={() => pick(index, 'own')}
                aria-label="Your own answer"
                placeholder="Something else: type your own answer"
                style={{ minWidth: 0, height: 26, padding: 0, border: 0, outline: 'none', background: 'transparent', fontSize: 'var(--t-body)', color: 'var(--text-primary)' }}
              />
            </label>
          </div>
          </fieldset>
        </div>
      ))}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '12px 14px' }}>
        <button type="submit" className="btn-p" disabled={!ready}>
          Answer
          <span className="mono" style={{ fontSize: 'var(--t-tag)', fontWeight: 400, opacity: 0.75 }}>
            ⏎
          </span>
        </button>
        <button type="button" className="btn-g" onClick={skip}>
          Skip
        </button>
        <span className="mono" style={{ marginLeft: 'auto', fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
          1–{card.items[0]?.options.length ?? 0} pick · ⏎ answer
        </span>
      </div>
    </form>
  )
}
