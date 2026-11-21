import { useObserved } from '../../styles/Observation/useObserved'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'

/** Regimes (design 1j): the regimes `train regime list` carries, and the one chosen with its kits in order -- the old `trn.regOn` block. */
export function RegimesView({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const regimes = model.face().regimes
  const detail = regimes.detail
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <nav aria-label="Regimes" style={{ flex: '0 0 280px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', background: 'var(--surface-sidebar)', borderRight: '1px solid var(--line-border)' }}>
        {regimes.rows.map((regime) => (
          <button
            key={regime.name}
            type="button"
            className="hv"
            onClick={() => model.selectRegime(regime.name)}
            aria-current={regime.current}
            style={{ width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '10px 12px', marginBottom: 4, border: 0, borderRadius: 7, background: regime.current ? 'var(--surface-selection)' : '', textAlign: 'left', color: 'var(--text-primary)' }}
          >
            <span>{regime.name}</span>
            <span style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{regime.meta}</span>
          </button>
        ))}
        {regimes.failure !== '' ? <p style={{ margin: '2px 12px 0', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{regimes.failure}</p> : null}
      </nav>
      <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '22px 28px 32px' }}>
        {detail.shown ? (
          <div style={{ maxWidth: 760 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
              <h2 style={{ flex: '1 1 auto', margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{detail.name}</h2>
              <button type="button" className="btn-g" onClick={() => model.runRegime()}>
                Run this regime
              </button>
            </div>
            <p style={{ margin: '8px 0 0', color: 'var(--text-secondary)' }}>{detail.description}</p>
            <h3 className="lbl" style={{ margin: '20px 0 6px' }}>
              KITS, IN ORDER
            </h3>
            {detail.kits.map((kit) => (
              <div key={kit}>{kit}</div>
            ))}
          </div>
        ) : null}
      </div>
    </div>
  )
}
