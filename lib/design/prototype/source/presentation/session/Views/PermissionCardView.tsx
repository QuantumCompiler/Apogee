import { useState } from 'react'
import { Card } from '../Components/Card'
import type { RowFace } from '../ViewModels/SessionViewModel'
import { QuietLine } from './TranscriptRow'

type PermissionFace = Extract<RowFace, { kind: 'permission' }>

/**
 * The change a file writer asks to make, as the CLI's `detail` carries it: a
 * unified diff, each line in its color -- the hunk header tertiary, a removal
 * in the error color, an addition in the accent (`main.html:382-398` at
 * `b375fac9`). The Mac app's `DiffView`.
 */
export function DiffView({ diff }: { readonly diff: string }) {
  const lines = diff.replace(/\n$/, '').split('\n')
  const color = (line: string) =>
    line.startsWith('+++') || line.startsWith('---') || line.startsWith('@@')
      ? 'var(--text-tertiary)'
      : line.startsWith('+')
        ? 'var(--status-accent)'
        : line.startsWith('-')
          ? 'var(--status-error)'
          : 'var(--text-secondary)'
  return (
    <div className="mono" style={{ padding: '8px 12px 10px', borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-meta)', lineHeight: 1.7, whiteSpace: 'pre', overflowX: 'auto', color: 'var(--text-secondary)' }}>
      {lines.map((line, index) => (
        <div key={index} style={{ color: color(line) }}>
          {line === '' ? ' ' : line}
        </div>
      ))}
    </div>
  )
}

/**
 * The permission gate's question (design 1v): exactly what would happen --
 * the tool, its whole target, the change folded with Show changes -- and the
 * four answers the protocol takes, as the old prototype's card draws them
 * (`main.html:370-409` at `b375fac9`). Denying is normal: the model is told
 * and carries on, as the old note under a denial says. Answered, it
 * collapses to its line; closed by its turn's end, to the line that says it
 * went unanswered. The Mac app's `PermissionCardView`.
 */
export function PermissionCardView({ card, answer, shown = false }: { readonly card: PermissionFace; readonly answer: (answer: string) => void; readonly shown?: boolean }) {
  const [diffShown, setDiffShown] = useState(shown)
  const subject = `${card.tool} ${card.target}`.trim()
  if (card.answered !== '') {
    // Denied, the old note under the Mac's quiet line: the model was told and carried on (`main.html:424` at `b375fac9`).
    return (
      <>
        <QuietLine text={`${subject} — ${card.answered}`} />
        {card.deniedNote !== '' ? (
          <div role="note" className="note">
            <span className="rule" />
            <span>{card.deniedNote}</span>
            <span className="rule" />
          </div>
        ) : null}
      </>
    )
  }
  if (card.closed) return <QuietLine text={`${subject} — not answered: the turn ended first`} />
  return (
    <Card label={`Permission request: ${card.title.replace(/\?$/, '').toLowerCase()}`}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '12px 14px 10px' }}>
        <span aria-hidden="true" style={{ width: 28, height: 28, display: 'grid', placeItems: 'center', borderRadius: 6, background: 'var(--surface-control)' }}>
          <svg className="i" width="15" height="15" viewBox="0 0 16 16">
            <path d="M10.5 3l2.5 2.5L6 12.5H3.5V10z" />
          </svg>
        </span>
        <div style={{ flex: '1 1 auto', minWidth: 0 }}>
          <div style={{ fontSize: 'var(--t-title)', fontWeight: 600 }}>{card.title}</div>
          <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{card.subtitle}</div>
        </div>
        <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          {card.tool}
        </span>
      </div>
      <div style={{ margin: '0 14px', border: '1px solid var(--line-border)', borderRadius: 6, background: 'var(--surface-code)', overflow: 'hidden' }}>
        <div style={{ padding: '10px 12px' }}>
          <div className="mono" style={{ fontSize: 'var(--t-code)', overflowWrap: 'anywhere' }}>
            {card.target}
          </div>
          {card.diff !== '' ? (
            <div className="mono" style={{ display: 'flex', alignItems: 'center', gap: 10, marginTop: 4, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
              <span>
                <span style={{ color: 'var(--status-accent)' }}>+{card.added}</span> <span style={{ color: 'var(--status-error)' }}>−{card.removed}</span> lines
              </span>
              <span>·</span>
              <button
                type="button"
                onClick={() => setDiffShown(!diffShown)}
                aria-expanded={diffShown}
                className="mono"
                style={{ height: 20, padding: '0 4px', border: 0, borderRadius: 4, fontSize: 'var(--t-meta)', color: 'var(--status-accent)' }}
              >
                {diffShown ? 'Hide changes' : 'Show changes'}
              </button>
            </div>
          ) : null}
        </div>
        {diffShown && card.diff !== '' ? <DiffView diff={card.diff} /> : null}
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 8, padding: '12px 14px' }}>
        <button type="button" className="btn-p" onClick={() => answer('yes')}>
          Allow once
          <span className="mono" style={{ fontSize: 'var(--t-tag)', fontWeight: 400, opacity: 0.75 }}>
            ⏎
          </span>
        </button>
        <button type="button" className="btn" onClick={() => answer('session')}>
          Allow for this session
        </button>
        <button type="button" className="btn" onClick={() => answer('always')}>
          Always allow
        </button>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="btn-g" onClick={() => answer('no')}>
          Deny
          <span className="mono" style={{ fontSize: 'var(--t-tag)', opacity: 0.75 }}>
            esc
          </span>
        </button>
      </div>
      <div style={{ padding: '9px 14px', borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>
        Always allow lets <span className="mono">{card.tool}</span> {card.tool === 'run_command' ? 'run commands' : 'change files'} without asking, in every session. Undo it in Settings › Permissions.
      </div>
    </Card>
  )
}
