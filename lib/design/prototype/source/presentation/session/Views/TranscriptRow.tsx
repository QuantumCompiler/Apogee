import { useState } from 'react'
import { MarkdownText } from '../../styles/Components/MarkdownText'
import { Card, CardLabel } from '../Components/Card'
import type { RowFace, SourceFace } from '../ViewModels/SessionViewModel'
import { PermissionCardView } from './PermissionCardView'
import { QuestionCardView } from './QuestionCardView'
import { kindColor, ModelGlyph } from './SessionExtras'
import { Icon } from '../../styles/Components/Icon'

type ActivityFace = Extract<RowFace, { kind: 'activity' }>
type AnswerFace = Extract<RowFace, { kind: 'answer' }>

/** What a row can do, handed down by the window: each a method of the session's view-model. A preview hands in none. */
export interface RowActs {
  readonly answer: (id: number, answers: readonly string[]) => void
  readonly skip: (id: number) => void
  readonly openCitation: (row: number, index: number) => void
  readonly closeCitation: () => void
  readonly openSource: () => void
  readonly resend: (text: string) => void
  readonly startAgain: () => void
  readonly retry: () => void
  readonly askInstead: (model: string) => void
  readonly openChat: (id: string) => void
}

const nothing = () => {}
/** The acts of a row drawn on its own: none does anything. */
export const noActs: RowActs = { answer: nothing, skip: nothing, openCitation: nothing, closeCitation: nothing, openSource: nothing, resend: nothing, startAgain: nothing, retry: nothing, askInstead: nothing, openChat: nothing }

const apiGlyph = <ModelGlyph kind="api" size={12} />


/**
 * A line the session said for the person, or what this app does not draw
 * yet: quiet, kept, in the mono face (the old "thought for" line's style).
 * The Mac app's `QuietLine`. An end is said in the error color.
 */
export function QuietLine({ text, tone = 'quiet' }: { readonly text: string; readonly tone?: 'quiet' | 'error' }) {
  return (
    <div
      role="note"
      className="mono"
      style={{ display: 'flex', alignItems: 'center', gap: 6, minHeight: 22, fontSize: 'var(--t-ui)', color: tone === 'error' ? 'var(--status-error)' : 'var(--text-tertiary)', overflowWrap: 'anywhere' }}
    >
      {text}
    </div>
  )
}

/**
 * A turn's activity: its reasoning and the lines of what it ran, each as the
 * CLI wrote it. A play is its card, open, its stages marked where they stand
 * (`main.html:590-596` at `b375fac9`); any other turn's thinking is open
 * while live and closed to one line after, a click away. The Mac app's
 * `ActivityView`.
 */
export function ActivityView({ activity }: { readonly activity: ActivityFace }) {
  const play = activity.lines.some((line) => line.mark !== '')
  const [expanded, setExpanded] = useState(false)
  // A live or waiting activity opens by itself, and its head closes it, as the old toggle does (`tg.toggleThink`, design 1s); a play's stands open, its head plain words, as the old's.
  const [shut, setShut] = useState(false)
  const opensItself = activity.live || activity.waiting
  const open = play || (opensItself ? !shut : expanded)
  const toggle = (to: boolean) => (opensItself ? setShut(!to) : setExpanded(to))
  const [lead, ...rest] = activity.summary.split(' · ')
  const detail = activity.reasoning !== '' || activity.lines.length > 0
  if (!open) {
    const line = { display: 'flex', alignItems: 'center', gap: 6, height: 22, padding: 0, border: 0, fontSize: 'var(--t-ui)', color: 'var(--text-tertiary)', textAlign: 'left' } as const
    const chevron = (
      <Icon name="chevronRight" size={12} />
    )
    // A turn that thought and said nothing of it has nothing to open.
    if (!detail) {
      return (
        <div className="mono" style={line}>
          {chevron}
          {activity.summary}
        </div>
      )
    }
    return (
      <button type="button" className="mono" aria-expanded={false} onClick={() => toggle(true)} style={line}>
        {chevron}
        {activity.summary}
      </button>
    )
  }
  const headStyle = { width: '100%', boxSizing: 'border-box', display: 'flex', alignItems: 'center', gap: 8, height: 32, padding: '0 12px', border: 0, borderBottom: detail ? '1px solid var(--line-border)' : 0, fontSize: 'var(--t-ui)', color: 'var(--text-secondary)', textAlign: 'left' } as const
  const head = (
    <>
      {activity.live ? (
        <span className="pulse" aria-hidden="true" style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--status-accent)' }} />
      ) : (
        <Icon name="chevronDown" size={12} style={{ color: 'var(--text-tertiary)' }} />
      )}
      <span style={{ color: 'var(--text-primary)', fontWeight: 500 }}>{lead}</span>
      {rest.length > 0 ? <span>· {rest.join(' · ')}</span> : null}
      {activity.waiting ? <span style={{ color: 'var(--status-warning)' }}>· paused · waiting for you</span> : null}
    </>
  )
  return (
    <div style={{ border: '1px solid var(--line-border)', borderRadius: 6, background: 'var(--surface-card)' }}>
      {play ? (
        <div className="mono" style={headStyle}>
          {head}
        </div>
      ) : (
        <button type="button" className="mono" aria-expanded={true} onClick={() => toggle(false)} style={headStyle}>
          {head}
        </button>
      )}
      {detail ? (
        <div style={{ padding: '10px 12px 12px' }}>
          {activity.reasoning !== '' ? (
            <p style={{ margin: activity.lines.length > 0 ? '0 0 8px' : 0, fontSize: 'var(--t-small)', color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
              {activity.reasoning}
              {activity.live ? <span className="caret" aria-hidden="true" style={{ display: 'inline-block', width: 7, height: 15, marginLeft: 2, verticalAlign: -2, background: 'var(--status-accent)' }} /> : null}
            </p>
          ) : null}
          {activity.lines.map((line, index) => (
            <div key={index} className="tl">
              <span style={{ color: 'var(--status-accent)' }}>{line.mark}</span>
              <span style={{ color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{line.text}</span>
              <span />
            </div>
          ))}
        </div>
      ) : null}
    </div>
  )
}

/** The source a citation opened (`main.html:459-463` at `b375fac9`): its file, the place in it, the CLI's score, and Open. The excerpt's words are not in the CLI's `sources` event, so none are drawn. */
function CitationCard({ source, close, open }: { readonly source: SourceFace; readonly close: () => void; readonly open: () => void }) {
  return (
    <div className="menu" style={{ margin: '0 0 12px', maxWidth: 460 }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '10px 14px', borderBottom: '1px solid var(--line-border)' }}>
        <span className="mono" style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-ui)' }}>
          {source.file}
        </span>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{source.place}</span>
        <button type="button" className="ib" onClick={close} aria-label="Close source" style={{ width: 24, height: 24 }}>
          <Icon name="close" size={11} />
        </button>
      </div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '8px 8px 8px 14px', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
        <span className="mono">{source.score}</span>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="btn" onClick={open}>
          {/^pages? /.test(source.place) ? `Open at ${source.place.replace(/^pages /, 'page ').replace(/–.*$/, '')}` : 'Open the file'}
        </button>
      </div>
    </div>
  )
}

/** An answer, as its deltas join: the markdown drawn, its `[n]` citations opening their source where its turn drew on some, then -- once its result has come -- the model that answered and the tokens it reported. */
function AnswerView({ answer, acts }: { readonly answer: AnswerFace; readonly acts: RowActs }) {
  const dot = kindColor(answer.footer.kind)
  const citations =
    answer.sources.length > 0
      ? { count: answer.sources.length, opened: answer.cited === 0 ? undefined : answer.cited, open: (index: number) => acts.openCitation(answer.id, index), label: (index: number) => `Source ${index}: ${answer.sources[index - 1]?.file ?? ''}${answer.sources[index - 1]?.place ? `, ${answer.sources[index - 1]?.place}` : ''}` }
      : undefined
  return (
    <>
      <MarkdownText text={answer.text} streaming={!answer.finished} citations={citations} />
      {answer.sources[answer.cited - 1] ? <CitationCard source={answer.sources[answer.cited - 1] as SourceFace} close={acts.closeCitation} open={acts.openSource} /> : null}
      {answer.finished && answer.sources.length > 0 ? <SourcesRow sources={answer.sources} open={(index) => acts.openCitation(answer.id, index)} /> : null}
      {answer.finished && (answer.footer.model !== '' || answer.footer.usage.length > 0) ? (
        <div className="mono" style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: '4px 16px', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          {answer.footer.model !== '' ? (
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
              {answer.footer.kind === 'api' ? (
                <span title="billed per use" style={{ display: 'inline-flex' }}>
                  <ModelGlyph kind="api" size={12} label="billed per use" />
                </span>
              ) : dot ? (
                <span aria-hidden="true" style={{ width: 6, height: 6, borderRadius: '50%', background: dot }} />
              ) : null}
              {answer.footer.model}
            </span>
          ) : null}
          {answer.footer.usage.map((count) => (
            <span key={count}>{count}</span>
          ))}
        </div>
      ) : null}
    </>
  )
}

/** The excerpts a turn drew on, numbered as the answer cites them (`main.html:485-492` at `b375fac9`): each its file, the place in it and the CLI's score, a click opening it. The Mac app's `SourcesRow`. */
export function SourcesRow({ sources, open }: { readonly sources: readonly SourceFace[]; readonly open: (index: number) => void }) {
  return (
    <div style={{ paddingTop: 10, borderTop: '1px solid var(--line-border)' }}>
      <div className="lbl" style={{ marginBottom: 6 }}>
        SOURCES · {sources.length}
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        {sources.map((source) => (
          <button
            key={source.index}
            type="button"
            className="hv"
            onClick={() => open(source.index)}
            style={{ display: 'grid', gridTemplateColumns: '18px minmax(0, 1fr) auto', gap: 10, alignItems: 'center', padding: '3px 6px', margin: '0 -6px', border: 0, borderRadius: 5, color: 'var(--text-primary)', fontSize: 'var(--t-dense)', textAlign: 'left' }}
          >
            <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--status-accent)' }}>
              {source.index}
            </span>
            <span style={{ minWidth: 0, overflowWrap: 'anywhere' }}>
              <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
                {source.file}
              </span>
              {source.place !== '' ? <span style={{ color: 'var(--text-tertiary)' }}> · {source.place}</span> : null}
            </span>
            <span className="mono" style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
              {source.score}
            </span>
          </button>
        ))}
      </div>
    </div>
  )
}

/**
 * The session is over (`main.html:669-679` at `b375fac9`): why, in the CLI's
 * words -- its exit and its stderr's last line -- the details behind it, and
 * Start again, which resumes a saved conversation and sends what was held
 * for it. The Mac app's `EndedCard`.
 */
export function EndedCard({ text, details, canStartAgain, startAgain, open: initial = false }: {
  readonly text: string
  readonly details: string
  readonly canStartAgain: boolean
  readonly startAgain: () => void
  /** Whether Details is open as it is drawn: a preview's. */
  readonly open?: boolean
}) {
  const [open, setOpen] = useState(initial)
  return (
    <div className="row">
      <Card accent="var(--status-error)" padded label="Session ended">
        <CardLabel text="Session ended" />
        <p style={{ margin: 0, fontSize: 'var(--t-body)', color: 'var(--text-primary)', overflowWrap: 'anywhere' }}>{text}</p>
        {details !== '' && details !== text ? (
          <>
            <button
              type="button"
              className="mono hv"
              onClick={() => setOpen(!open)}
              aria-expanded={open}
              style={{ display: 'inline-flex', alignItems: 'center', gap: 6, height: 24, padding: '0 8px 0 4px', marginLeft: -4, border: 0, borderRadius: 4, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}
            >
              <Icon name="chevronRight" size={12} style={{ transform: `rotate(${open ? 90 : 0}deg)` }} />
              Details
            </button>
            {open ? (
              <div className="mono" style={{ alignSelf: 'stretch', padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
                {details}
              </div>
            ) : null}
          </>
        ) : null}
        {canStartAgain ? (
          <button type="button" className="btn" onClick={startAgain}>
            Start again
          </button>
        ) : null}
      </Card>
    </div>
  )
}

/** A turn whose line never reached the child (`main.html:660-668` at `b375fac9`): said so, and Send again once a child takes a turn. */
function NotSentCard({ row, resend }: { readonly row: Extract<RowFace, { kind: 'notSent' }>; readonly resend: (text: string) => void }) {
  return (
    <div className="row">
      <Card accent="var(--status-warning)" padded label="Not sent">
        <CardLabel text="Not sent" />
        <p style={{ margin: 0, fontSize: 'var(--t-body)', color: 'var(--text-primary)' }}>apogee had stopped taking input, so this message never reached it.</p>
        {row.canResend ? (
          <button type="button" className="btn" onClick={() => resend(row.text)} disabled={!row.enabled} title="Sends the message above again, as typed">
            Send again
          </button>
        ) : null}
      </Card>
    </div>
  )
}

/**
 * A turn that failed (`main.html:507-512` at `b375fac9`): a model that did not
 * load is the old prototype's card -- its words the CLI's, its details the
 * whole message, Unload and retry, and the billed backend to ask instead,
 * marked billed -- and any other failure the Mac app's card, The turn failed
 * and Try again.
 */
function ErrorCard({ row, acts, open: initial = false }: { readonly row: Extract<RowFace, { kind: 'error' }>; readonly acts: RowActs; readonly open?: boolean }) {
  const [open, setOpen] = useState(initial)
  if (!row.loadFailed) {
    return (
      <Card accent="var(--status-error)" padded label="The turn failed">
        <CardLabel text="The turn failed" />
        <p style={{ margin: 0, fontSize: 'var(--t-body)', color: 'var(--text-primary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>{row.details === '' ? row.message : row.details}</p>
        {row.canRetry ? (
          <button type="button" className="btn" onClick={acts.retry}>
            Try again
          </button>
        ) : null}
      </Card>
    )
  }
  return (
    <Card role="alert">
      <div style={{ display: 'flex', alignItems: 'flex-start', gap: 10, padding: '14px 14px 10px' }}>
        <Icon name="error" size={16} style={{ marginTop: 2, color: 'var(--status-error)' }} />
        <div style={{ minWidth: 0 }}>
          <div style={{ fontSize: 'var(--t-title)', fontWeight: 600 }}>{row.title}</div>
          <div style={{ marginTop: 2, fontSize: 'var(--t-dense)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{row.message}</div>
        </div>
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 8, padding: '2px 14px 12px 40px' }}>
        <button type="button" className="btn-p" onClick={acts.retry} disabled={!row.canRetry}>
          Unload and retry
        </button>
        {row.instead !== '' ? (
          <button type="button" className="btn" onClick={() => acts.askInstead(row.instead)} disabled={!row.canRetry}>
            {apiGlyph}
            Ask {row.instead} instead · billed
          </button>
        ) : null}
        {row.details !== '' ? (
          <button
            type="button"
            className="mono hv"
            onClick={() => setOpen(!open)}
            aria-expanded={open}
            style={{ height: 28, padding: '0 8px', border: 0, borderRadius: 6, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}
          >
            details
          </button>
        ) : null}
      </div>
      {open ? (
        <div className="mono" style={{ padding: '9px 14px', borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
          {row.details}
        </div>
      ) : null}
    </Card>
  )
}

/** A notice between its rules; a switch to a model billed per use marked so, with the API's glyph (`main.html:446` at `b375fac9`). */
function NoticeRow({ row }: { readonly row: Extract<RowFace, { kind: 'notice' }> }) {
  return (
    <div role="note" className="note">
      <span className="rule" />
      {row.billed ? apiGlyph : null}
      <span style={{ overflowWrap: 'anywhere', textAlign: 'center' }}>
        {row.text}
        {row.billed ? <span style={{ color: 'var(--model-api)' }}> · billed per use from here</span> : null}
      </span>
      <span className="rule" />
    </div>
  )
}

/** What a turn recalled, each chat by name, openable (`main.html:309` at `b375fac9`; the Mac app's recalled row). */
function RecallRow({ row, open }: { readonly row: Extract<RowFace, { kind: 'recalled' }>; readonly open: (id: string) => void }) {
  return (
    <div className="mono" style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8, fontSize: 'var(--t-ui)', color: 'var(--text-secondary)' }}>
      <Icon name="memory" size={13} style={{ color: 'var(--text-tertiary)' }} />
      <span>{row.summary}</span>
      {row.chats.map((chat) => (
        <span key={chat.id} style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
          <span style={{ color: 'var(--text-tertiary)' }}>·</span>
          <button type="button" onClick={() => open(chat.id)} className="mono" title="Open this session" style={{ padding: 0, border: 0, fontSize: 'var(--t-ui)', color: 'var(--text-secondary)', textDecoration: 'underline', textDecorationColor: 'var(--line-strong)', textUnderlineOffset: 3 }}>
            {chat.name}
          </button>
          {chat.updated !== '' ? <span style={{ color: 'var(--text-tertiary)' }}>{chat.updated}</span> : null}
        </span>
      ))}
    </div>
  )
}

/**
 * One row of the conversation, drawn for what it is (`main.html:641-679` at
 * `b375fac9`): what the person sent, a notice between its rules, a warning
 * the read back gave, the divider where the live session takes over, a
 * turn's activity, its answer, a recall, its sources, the question and
 * permission cards, a failed turn, a turn not sent, the plan, a quiet line,
 * the session's end. The Mac app's `TranscriptRow`.
 */
export function TranscriptRow({ row, acts = noActs }: { readonly row: RowFace; readonly acts?: RowActs }) {
  switch (row.kind) {
    case 'user':
      return (
        <div className="row me">
          <p className="prose" style={{ margin: 0, minWidth: 0, whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
            {row.text}
          </p>
        </div>
      )
    case 'notice':
      return <NoticeRow row={row} />
    case 'warning':
      return (
        <div role="note" className="mono" style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-meta)', color: 'var(--status-warning)', overflowWrap: 'anywhere' }}>
          <Icon name="error" size={13} />
          <span>{row.text}</span>
        </div>
      )
    case 'divider':
      return (
        <div role="note" className="note">
          <span className="rule" />
          <Icon name="send" size={11} />
          <span>{row.text}</span>
          <span className="rule" />
        </div>
      )
    case 'quiet':
      return <QuietLine text={row.text} />
    case 'ended':
      return row.atLaunch ? (
        <QuietLine text={`apogee refused to start this session: ${row.text}`} tone="error" />
      ) : (
        <EndedCard text={row.text} details={row.details} canStartAgain={row.canStartAgain} startAgain={acts.startAgain} />
      )
    case 'activity':
      return <ActivityView activity={row} />
    case 'answer':
      return <AnswerView answer={row} acts={acts} />
    case 'recalled':
      return <RecallRow row={row} open={acts.openChat} />
    case 'sources':
      // Drawn after its turn's answer, as the old prototype draws them; on its own only where no answer came.
      return row.withAnswer ? null : <SourcesRow sources={row.sources} open={nothing} />
    case 'question':
      return <QuestionCardView card={row} answer={(answers) => acts.answer(row.id, answers)} skip={() => acts.skip(row.id)} />
    case 'permission':
      return <PermissionCardView card={row} answer={(answer) => acts.answer(row.id, [answer])} />
    case 'error':
      return <ErrorCard row={row} acts={acts} />
    case 'notSent':
      return <NotSentCard row={row} resend={acts.resend} />
    case 'todo':
      return (
        <Card padded label="Plan">
          <CardLabel text="Plan" />
          {row.items.map((entry, index) => (
            <span key={index} style={{ fontSize: 'var(--t-body)', color: entry.state === 'done' ? 'var(--text-tertiary)' : 'var(--text-primary)' }}>
              {entry.state === 'done' ? '✓' : entry.state === 'active' ? '◌' : '○'} {entry.text}
            </span>
          ))}
        </Card>
      )
  }
}
