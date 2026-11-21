import { useObserved } from '../../styles/Observation/useObserved'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'

/** Runs (design 1j): the runs `train status` lists, the promoted versions `train versions` keeps, and the run chosen -- the old `trn.runsOn` block. */
export function RunsView({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const runs = model.face().runs
  const detail = runs.detail
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '12px 10px' }}>
        {runs.rows.map((run) => (
          <button
            key={run.id}
            type="button"
            className="hv"
            onClick={() => model.selectRun(run.id)}
            aria-current={run.current}
            style={{ width: '100%', display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: '3px 12px', padding: '10px 12px', marginBottom: 2, border: 0, borderRadius: 7, background: run.current ? 'var(--surface-selection)' : '', textAlign: 'left', color: 'var(--text-primary)' }}
          >
            <span className="mono" style={{ fontSize: 'var(--t-body)' }}>
              {run.id}
            </span>
            <span style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>{run.status}</span>
            <span style={{ fontSize: 'var(--t-meta)', color: run.failed ? 'var(--status-error)' : 'var(--text-tertiary)' }}>{run.meta}</span>
          </button>
        ))}
        {runs.missing !== '' ? <p style={{ margin: '2px 12px 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{runs.missing}</p> : null}
        {runs.failure !== '' ? <p style={{ margin: '2px 12px 0', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{runs.failure}</p> : null}
        {runs.versionsShown ? (
          <>
            <h3 className="lbl" style={{ margin: '18px 12px 6px' }}>
              PROMOTED VERSIONS
            </h3>
            {runs.versionsFailure !== '' ? <p style={{ margin: '0 12px', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{runs.versionsFailure}</p> : null}
            {runs.versions.map((ledger) => (
              <div key={ledger.backend} style={{ padding: '6px 12px' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                  <span className="mono" style={{ flex: '1 1 auto', fontSize: 'var(--t-small)' }}>
                    {ledger.backend}
                  </span>
                  <button type="button" className="btn-g" onClick={() => void model.rollback(ledger.backend)} disabled={!ledger.canRoll} style={{ height: 24, fontSize: 'var(--t-small)' }}>
                    Roll back…
                  </button>
                </div>
                {ledger.lines.map((line) => (
                  <div key={line} className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                    {line}
                  </div>
                ))}
              </div>
            ))}
          </>
        ) : null}
      </div>
      <aside className="ap-detail" aria-label="Run" style={{ flex: '0 0 380px', minHeight: 0, overflowY: 'auto', padding: '18px 20px', background: 'var(--surface-inspector)', borderLeft: '1px solid var(--line-border)' }}>
        {detail.shown ? (
          <>
            <h2 className="mono" style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>
              {detail.id}
            </h2>
            <div style={{ marginTop: 6, color: detail.failed ? 'var(--status-error)' : 'var(--text-secondary)' }}>{detail.gate}</div>
            {detail.facts.map((fact) => (
              <div key={fact.label}>
                <h3 className="lbl" style={{ margin: '14px 0 4px' }}>
                  {fact.label}
                </h3>
                <div style={{ overflowWrap: 'anywhere' }}>{fact.value}</div>
              </div>
            ))}
            <div style={{ display: 'flex', gap: 8, marginTop: 18 }}>
              <button type="button" className="btn-g" onClick={() => model.gate()}>
                Gate it
              </button>
              <button type="button" className="btn-g" onClick={() => model.newPromotion()} title="apogee train promote: fuse, verify and register it as a backend -- or, with --adapter, the adapter alone on an entry's adapters">
                Promote…
              </button>
            </div>
          </>
        ) : (
          <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{runs.noneChosen}</p>
        )}
      </aside>
    </div>
  )
}
