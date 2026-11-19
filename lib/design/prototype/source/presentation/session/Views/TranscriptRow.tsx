import { useState } from 'react'
import { MarkdownText } from '../../styles/Components/MarkdownText'
import type { RowFace } from '../ViewModels/SessionViewModel'
import { kindColor } from './SessionExtras'

type ActivityFace = Extract<RowFace, { kind: 'activity' }>
type AnswerFace = Extract<RowFace, { kind: 'answer' }>

const chevronDown = <path d="M4.5 6.5L8 10l3.5-3.5" />
const chevronRight = <path d="M6.5 4.5L10 8l-3.5 3.5" />

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
  const open = activity.live || play || expanded
  const [lead, ...rest] = activity.summary.split(' · ')
  const detail = activity.reasoning !== '' || activity.lines.length > 0
  if (!open) {
    const line = { display: 'flex', alignItems: 'center', gap: 6, height: 22, padding: 0, border: 0, fontSize: 'var(--t-ui)', color: 'var(--text-tertiary)', textAlign: 'left' } as const
    const chevron = (
      <svg className="i" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true">
        {chevronRight}
      </svg>
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
      <button type="button" className="mono" aria-expanded={false} onClick={() => setExpanded(true)} style={line}>
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
        <svg className="i" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true" style={{ color: 'var(--text-tertiary)' }}>
          {chevronDown}
        </svg>
      )}
      <span style={{ color: 'var(--text-primary)', fontWeight: 500 }}>{lead}</span>
      {rest.length > 0 ? <span>· {rest.join(' · ')}</span> : null}
    </>
  )
  return (
    <div style={{ border: '1px solid var(--line-border)', borderRadius: 6, background: 'var(--surface-card)' }}>
      {activity.live || play ? (
        <div className="mono" style={headStyle}>
          {head}
        </div>
      ) : (
        <button type="button" className="mono" aria-expanded={true} onClick={() => setExpanded(false)} style={headStyle}>
          {head}
        </button>
      )}
      {detail ? (
        <div style={{ padding: '10px 12px 12px' }}>
          {activity.reasoning !== '' ? (
            <p style={{ margin: activity.lines.length > 0 ? '0 0 8px' : 0, fontSize: 'var(--t-small)', color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>{activity.reasoning}</p>
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

/** An answer, as its deltas join: the markdown drawn, then -- once its result has come -- the model that answered and the tokens it reported. */
function AnswerView({ answer }: { readonly answer: AnswerFace }) {
  const dot = kindColor(answer.footer.kind)
  return (
    <>
      <MarkdownText text={answer.text} streaming={!answer.finished} />
      {answer.finished && (answer.footer.model !== '' || answer.footer.usage.length > 0) ? (
        <div className="mono" style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: '4px 16px', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          {answer.footer.model !== '' ? (
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
              {dot ? <span aria-hidden="true" style={{ width: 6, height: 6, borderRadius: '50%', background: dot }} /> : null}
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

/**
 * One row of the conversation, drawn for what it is (`main.html:641-659` at
 * `b375fac9`): what the person sent, a notice between its rules, a turn's
 * activity, its answer, a quiet line, the session's end. The Mac app's
 * `TranscriptRow`. The session ended's card, Not sent and the held lines
 * are 1v's: until then an end is its quiet line.
 */
export function TranscriptRow({ row }: { readonly row: RowFace }) {
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
      return (
        <div role="note" className="note">
          <span className="rule" />
          <span style={{ overflowWrap: 'anywhere', textAlign: 'center' }}>{row.text}</span>
          <span className="rule" />
        </div>
      )
    case 'quiet':
      return <QuietLine text={row.text} />
    case 'ended':
      return <QuietLine text={row.atLaunch ? `apogee refused to start this session: ${row.text}` : row.text} tone="error" />
    case 'activity':
      return <ActivityView activity={row} />
    case 'answer':
      return <AnswerView answer={row} />
  }
}
