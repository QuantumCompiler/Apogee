import { useObserved } from '../../styles/Observation/useObserved'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'
import { StageList } from './PipelinesView'

const small = { margin: 0, fontSize: 'var(--t-small)' } as const

/**
 * The Cycle (design 1j): the unattended loop as `train cycle status` reads it
 * -- what holds it (a dead pass's lock, a halt, an unreadable history), the
 * pass running here, its facts and its passes, newest first -- the old
 * `trn.cyOn` block.
 */
export function CycleView({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const cycle = model.face().cycle
  const lock = cycle.lock
  const head = lock.slice(0, Math.ceil(lock.length / 2))
  const tail = lock.slice(Math.ceil(lock.length / 2))
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, overflowY: 'auto', padding: '22px 28px 32px' }}>
      <div style={{ maxWidth: 860, display: 'flex', flexDirection: 'column', gap: 16 }}>
        <p style={{ ...small, color: 'var(--text-secondary)' }}>
          The cycle is unattended by design: your own scheduler -- launchd or cron -- runs <span className="mono">apogee train cycle run</span>. Nothing here schedules it; Run one pass is a single pass, now.
        </p>
        {cycle.hold !== '' ? (
          <p role="status" style={{ ...small, color: 'var(--status-warning)', overflowWrap: 'anywhere', userSelect: 'text' }}>
            {cycle.hold}
          </p>
        ) : null}
        {cycle.keptHalt !== '' ? (
          <p role="status" className="mono" style={{ margin: 0, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
            {cycle.keptHalt}
          </p>
        ) : null}
        {lock !== '' ? (
          <div style={{ display: 'flex', alignItems: 'center', gap: 10, minWidth: 0 }}>
            {/* Cut in its middle when the row is narrow, as the app's truncationMode(.middle) cuts it: the second half kept whole, the first ellipsized. */}
            <span className="mono" style={{ flex: '0 1 auto', minWidth: 0, overflow: 'hidden', whiteSpace: 'nowrap', fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', userSelect: 'text' }}>
              <span style={{ display: 'inline-block', verticalAlign: 'bottom', maxWidth: `calc(100% - ${tail.length}ch)`, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{head}</span>
              {tail}
            </span>
            <button type="button" className="btn-g" onClick={() => model.showLockInFinder()} title="Shows cycle.lock in the Finder, for you to remove" style={{ flex: '0 0 auto' }}>
              Show in Finder
            </button>
          </div>
        ) : null}
        {cycle.live.shown ? (
          <div style={{ padding: '14px 16px', borderRadius: 8, background: 'var(--surface-card)' }}>
            <h3 className="lbl" style={{ margin: '0 0 8px' }}>
              THIS PASS, RUNNING HERE
            </h3>
            {cycle.live.lines.map((line, index) => (
              <div key={index} style={{ display: 'grid', gridTemplateColumns: '92px minmax(0, 1fr)', gap: 12, alignItems: 'baseline', padding: '2px 0' }}>
                <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                  {line.state}
                </span>
                <span style={{ fontSize: 'var(--t-small)', color: line.latest ? 'var(--text-primary)' : 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{line.text}</span>
              </div>
            ))}
            {cycle.live.starting ? <p style={{ ...small, color: 'var(--text-tertiary)' }}>Starting…</p> : null}
            {cycle.live.stages.length > 0 ? (
              <>
                <h3 className="lbl" style={{ margin: '14px 0 4px' }}>
                  STAGES
                </h3>
                <StageList stages={cycle.live.stages} />
              </>
            ) : null}
          </div>
        ) : null}
        {cycle.error !== '' ? (
          <div role="status" style={{ fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere', userSelect: 'text' }}>
            <p style={{ margin: 0 }}>{cycle.error}</p>
            <p style={{ margin: '12px 0 0' }}>{cycle.errorHeld}</p>
          </div>
        ) : null}
        {cycle.failure !== '' ? <p style={{ ...small, color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{cycle.failure}</p> : null}
        {cycle.noHistory !== '' ? <p style={{ ...small, color: 'var(--text-tertiary)' }}>{cycle.noHistory}</p> : null}
        {cycle.hasHistory ? (
          <>
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '14px 36px' }}>
              {cycle.facts.map((fact) => (
                <div key={fact.label}>
                  <h3 className="lbl" style={{ margin: '0 0 4px' }}>
                    {fact.label}
                  </h3>
                  <div>{fact.value}</div>
                </div>
              ))}
            </div>
            <h3 className="lbl" style={{ margin: '8px 0 0' }}>
              PASSES, NEWEST FIRST
            </h3>
            {cycle.passes.length === 0 ? <p style={{ ...small, color: 'var(--text-tertiary)' }}>None yet.</p> : null}
            {cycle.passes.map((pass, index) => (
              <div key={`${pass.at}-${index}`} style={{ padding: '2px 0' }}>
                <div className="mono" style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 14px', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                  <span>{pass.at}</span>
                  {pass.pipeline !== '' ? <span>pipeline {pass.pipeline}</span> : null}
                </div>
                <div style={{ marginTop: 2, color: pass.failed ? 'var(--status-error)' : pass.passed ? 'var(--text-primary)' : 'var(--text-secondary)' }}>{pass.line}</div>
                {pass.reason !== '' ? <p style={{ margin: '2px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{pass.reason}</p> : null}
                {pass.note !== '' ? <p style={{ margin: '2px 0 0', fontSize: 'var(--t-small)', color: 'var(--status-warning)' }}>{pass.note}</p> : null}
              </div>
            ))}
          </>
        ) : null}
      </div>
    </div>
  )
}
