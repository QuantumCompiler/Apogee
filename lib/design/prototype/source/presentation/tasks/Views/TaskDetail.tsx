import { useObserved } from '../../styles/Observation/useObserved'
import type { TasksViewModel } from '../ViewModels/TasksViewModel'
import { toneColor } from './TaskTones'

const heading = (margin: string) => ({ margin }) as const

/**
 * The task's page: its goal and meta, its status and the controls -- Stop
 * after this round, Stop now, Resume or Resuming… and where, Run again --
 * the interrupted line, Budget, Grants, the plan, the rounds, Allowed and
 * Denied, the checks, Recent activity and the line on asking.
 */
export function TaskDetail({ model }: { readonly model: TasksViewModel }) {
  useObserved(model)
  const face = model.face()
  const detail = face.detail
  if (!detail.shown) return face.failure === '' ? null : <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{face.failure}</p>
  return (
    <div style={{ maxWidth: 860 }}>
      <div style={{ display: 'flex', alignItems: 'flex-start', flexWrap: 'wrap', gap: 12 }}>
        <div style={{ flex: '1 1 360px', minWidth: 0 }}>
          <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{detail.goal}</h2>
          <p className="mono" style={{ margin: '6px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            {detail.meta}
          </p>
        </div>
        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, height: 28, fontSize: 'var(--t-small)', color: toneColor[detail.tone] }}>
          {detail.isRunning ? <span className="pulse" style={{ width: 7, height: 7, borderRadius: '50%', background: 'var(--status-accent)' }} /> : null}
          {detail.status}
        </span>
        {detail.canPause ? (
          <button type="button" className="btn" onClick={() => void model.halt()} title="apogee task halt">
            Stop after this round
          </button>
        ) : null}
        {detail.canStop ? (
          <button type="button" className="btn-g" onClick={() => void model.cancel()} title="apogee task cancel">
            Stop now
          </button>
        ) : null}
        {detail.canResume ? (
          <button type="button" className="btn-p" onClick={() => model.resume()} title="apogee task resume">
            Resume
          </button>
        ) : null}
        {detail.isResuming ? (
          <button type="button" className="btn-p" disabled title="apogee task resume">
            Resuming…
          </button>
        ) : null}
        {detail.showWhere ? (
          <span className="mono" style={{ alignSelf: 'center', minWidth: 0, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
            {detail.resumeWhere}
          </span>
        ) : null}
        {detail.canRerun ? (
          <button type="button" className="btn-p" onClick={() => model.rerun()}>
            {detail.rerunLabel}
          </button>
        ) : null}
      </div>
      {detail.interrupted ? <p style={{ margin: '12px 0 0', fontSize: 'var(--t-small)', color: 'var(--status-warning)' }}>{detail.interruptedLine}</p> : null}
      <h3 className="lbl" style={heading('24px 0 10px')}>
        BUDGET
      </h3>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: 12 }}>
        {detail.budget.map((card) => (
          <div key={card.label} className="card" style={{ padding: '12px 14px' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 'var(--t-small)' }}>
              <span style={{ color: 'var(--text-secondary)' }}>{card.label}</span>
              <span className="mono" style={{ fontSize: 'var(--t-ui)', color: toneColor[card.tone] }}>
                {card.value}
              </span>
            </div>
            {card.hasBar ? (
              <div style={{ height: 5, marginTop: 8, borderRadius: 3, background: 'var(--line-border)', overflow: 'hidden' }}>
                <div style={{ width: `${card.pct}%`, height: '100%', background: toneColor[card.tone] }} />
              </div>
            ) : null}
          </div>
        ))}
      </div>
      {detail.grants.shown ? (
        <>
          <h3 className="lbl" style={heading('24px 0 8px')}>
            GRANTS
          </h3>
          <p className="mono" style={{ margin: 0, fontSize: 'var(--t-ui)', overflowWrap: 'anywhere' }}>
            {detail.grants.text}
          </p>
        </>
      ) : null}
      <h3 className="lbl" style={heading('24px 0 8px')}>
        PLAN · FROM THE PLAN TURN
      </h3>
      <ol style={{ margin: 0, padding: 0, listStyle: 'none', display: 'flex', flexDirection: 'column' }}>
        {detail.plan.map((step) => (
          <li key={step.n} style={{ display: 'grid', gridTemplateColumns: '22px minmax(0, 1fr)', gap: 8, padding: '6px 0', fontSize: 'var(--t-body)' }}>
            <span className="mono" style={{ fontSize: 'var(--t-ui)', color: 'var(--text-tertiary)' }}>
              {step.n}.
            </span>
            <span>{step.text}</span>
          </li>
        ))}
      </ol>
      <h3 className="lbl" style={heading('24px 0 8px')}>
        ROUNDS
      </h3>
      <div style={{ display: 'flex', flexDirection: 'column' }}>
        {detail.rounds.map((round) => (
          <div key={round.n} style={{ display: 'grid', gridTemplateColumns: '70px 64px minmax(0, 1fr) auto 52px', gap: 10, alignItems: 'baseline', padding: '8px 0', borderBottom: '1px solid var(--line-border)' }}>
            <span style={{ fontSize: 'var(--t-body)', fontWeight: 500 }}>{round.n}</span>
            <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
              {round.kind}
            </span>
            <span style={{ minWidth: 0, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
              {round.text}
              {round.denied !== '' ? <span style={{ display: 'block', marginTop: 2, fontSize: 'var(--t-meta)', color: 'var(--status-warning)', overflowWrap: 'anywhere' }}>{round.denied}</span> : null}
            </span>
            <span style={{ fontSize: 'var(--t-small)', color: toneColor[round.tone] }}>{round.outcome}</span>
            <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', textAlign: 'right' }}>
              {round.tokens}
            </span>
          </div>
        ))}
      </div>
      {detail.allowed.length > 0 ? (
        <>
          <h3 className="lbl" style={heading('24px 0 8px')}>
            ALLOWED
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            {detail.allowed.map((line) => (
              <div key={line} className="mono" style={{ fontSize: 'var(--t-ui)', overflowWrap: 'anywhere' }}>
                {line}
              </div>
            ))}
          </div>
        </>
      ) : null}
      {detail.denied.length > 0 ? (
        <>
          <h3 className="lbl" style={heading('24px 0 8px')}>
            DENIED
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            {detail.denied.map((line) => (
              <div key={line} className="mono" style={{ fontSize: 'var(--t-ui)', color: 'var(--status-warning)', overflowWrap: 'anywhere' }}>
                {line}
              </div>
            ))}
          </div>
        </>
      ) : null}
      <h3 className="lbl" style={heading('24px 0 8px')}>
        CHECKS · WHAT DONE MEANS
      </h3>
      {detail.noChecks ? <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>No checks: the task is done when the model says it is.</p> : null}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        {detail.checks.map((check) => (
          <div key={check.cmd} style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: 12, alignItems: 'baseline' }}>
            <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
              {check.cmd}
            </span>
            <span style={{ fontSize: 'var(--t-small)', color: toneColor[check.tone] }}>{check.result}</span>
          </div>
        ))}
      </div>
      <h3 className="lbl" style={heading('24px 0 8px')}>
        RECENT ACTIVITY
      </h3>
      <div className="card" style={{ padding: '8px 14px' }}>
        {detail.activity.map((line, index) => (
          <div key={`${index}-${line.tool}`} className="tl">
            <span>{line.tool}</span>
            <span style={{ color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{line.target}</span>
            <span style={{ color: toneColor[line.tone] }}>{line.result}</span>
          </div>
        ))}
        {detail.refused !== '' ? (
          <div className="mono" style={{ padding: '2px 0', fontSize: 'var(--t-ui)', lineHeight: '18px', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
            {detail.refused}
          </div>
        ) : null}
      </div>
      <p style={{ margin: '12px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{detail.perms}</p>
    </div>
  )
}
