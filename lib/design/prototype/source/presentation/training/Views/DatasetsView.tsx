import { useObserved } from '../../styles/Observation/useObserved'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'

/** The pulled dataset's banner (4i's `PulledBanner`): what the pull said, where it landed, Prepare it and Dismiss. */
export function PulledBanner({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const pulled = model.face().datasets.pulled
  if (!pulled.shown) return null
  return (
    <div role="status" style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '10px 24px', borderBottom: '1px solid var(--line-border)' }}>
      <div style={{ flex: '1 1 auto', minWidth: 0 }}>
        <div style={{ fontSize: 'var(--t-body)' }}>{pulled.line}</div>
        <div className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
          {pulled.path}
        </div>
      </div>
      <button type="button" className="btn" onClick={() => model.prepare()} disabled={!pulled.canPrepare} title={pulled.command}>
        Prepare it
      </button>
      <button type="button" className="btn-g" onClick={() => model.dismissPulled()}>
        Dismiss
      </button>
    </div>
  )
}

/** Datasets (design 1j): the pulled banner, the datasets `datasets list` names, the kits `datasets kits` gates on, and the dataset chosen -- the old `trn.dsOn` block. */
export function DatasetsView({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const datasets = model.face().datasets
  const detail = datasets.detail
  return (
    <>
      <PulledBanner model={model} />
      <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
        <nav aria-label="Datasets" style={{ flex: '0 0 300px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', background: 'var(--surface-sidebar)', borderRight: '1px solid var(--line-border)' }}>
          {datasets.rows.map((dataset) => (
            <button
              key={dataset.name}
              type="button"
              className="hv"
              onClick={() => model.selectDataset(dataset.name)}
              aria-current={dataset.current}
              style={{ width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '10px 12px', border: 0, borderRadius: 7, background: dataset.current ? 'var(--surface-selection)' : '', textAlign: 'left', color: 'var(--text-primary)' }}
            >
              <span>{dataset.name}</span>
              <span style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{dataset.meta}</span>
            </button>
          ))}
          {datasets.missing !== '' ? <p style={{ margin: '2px 12px 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{datasets.missing}</p> : null}
          {datasets.failure !== '' ? <p style={{ margin: '2px 12px 0', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{datasets.failure}</p> : null}
          <h3 className="lbl" style={{ margin: '18px 12px 6px' }}>
            KITS
          </h3>
          {datasets.kits.map((kit) => (
            <div key={kit.name} style={{ padding: '4px 12px' }}>
              <div style={{ fontSize: 'var(--t-small)' }}>{kit.name}</div>
              <div style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{kit.meta}</div>
            </div>
          ))}
          {datasets.kitsFailure !== '' ? <p style={{ margin: '2px 12px 0', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{datasets.kitsFailure}</p> : null}
        </nav>
        <div style={{ flex: '1 1 0', minWidth: 0, padding: '22px 28px' }}>
          {detail.shown ? (
            <>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                <h2 style={{ flex: '1 1 auto', margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{detail.name}</h2>
                <button type="button" className="btn-g" onClick={() => model.deleteDataset()} style={{ color: 'var(--status-error)' }}>
                  Delete…
                </button>
              </div>
              <p style={{ margin: '8px 0 0' }}>{detail.line}</p>
              <div className="mono" style={{ marginTop: 4, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                {detail.path}
              </div>
            </>
          ) : (
            <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{datasets.noneChosen}</p>
          )}
        </div>
      </div>
    </>
  )
}
