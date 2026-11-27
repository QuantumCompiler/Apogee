import { useObserved } from '../../styles/Observation/useObserved'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'
import { CycleView } from './CycleView'
import { DatasetsView } from './DatasetsView'
import { PipelinesView } from './PipelinesView'
import { RegimesView } from './RegimesView'
import { RunsView } from './RunsView'

/**
 * The Training place (design 1j): the old prototype's screen (`main.html` at
 * `b375fac9`, `ui.isTraining`, 1279-1400) -- its header with the five tabs
 * and the tab's own buttons, the bar of the act running now, then Runs,
 * Pipelines, Cycle, Regimes or Datasets -- drawn from the place's view-model.
 */
export function TrainingScreen({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const face = model.face()
  const { header, bar } = face
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 14, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Training</h1>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Fine-tune a stored model, gate it, promote it</span>
        <span style={{ flex: '1 1 auto' }} />
        <div role="group" aria-label="Show" className="seg">
          {face.tabs.map((tab) => (
            <button key={tab.tab} type="button" aria-pressed={tab.current} onClick={() => model.pick(tab.tab)} style={{ background: tab.current ? 'var(--surface-control)' : 'transparent', color: 'var(--text-primary)' }}>
              {tab.label}
            </button>
          ))}
        </div>
        {header.newRun.shown ? (
          <button type="button" className="btn-g" onClick={() => model.newRun()} disabled={!header.newRun.enabled}>
            New run…
          </button>
        ) : null}
        {header.cycle.shown ? (
          <>
            <button type="button" className="btn-g" onClick={() => model.requestPass()} disabled={!header.cycle.canRun} title="apogee train cycle run">
              Run one pass…
            </button>
            {header.cycle.halted ? (
              <button type="button" className="btn-g" onClick={() => model.requestResumeCycle()} title="apogee train cycle resume">
                Resume…
              </button>
            ) : (
              <button type="button" className="btn-g" onClick={() => model.requestHalt()} disabled={!header.cycle.canHalt} title="apogee train cycle halt">
                Halt…
              </button>
            )}
          </>
        ) : null}
        {header.datasets.shown ? (
          <>
            <button type="button" className="btn-g" onClick={() => model.newDataset()} disabled={!header.datasets.enabled}>
              New dataset…
            </button>
            <button type="button" className="btn-g" onClick={() => model.newPull()} disabled={!header.datasets.canPull} title="apogee datasets pull owner/name: a Hugging Face dataset's data files, for Prepare">
              Pull…
            </button>
            <button type="button" className="btn-g" onClick={() => model.synthesize()} disabled={!header.datasets.enabled}>
              Synthesize…
            </button>
            <button type="button" className="btn-g" onClick={() => model.prepareFile()} disabled={!header.datasets.enabled}>
              Prepare a file…
            </button>
          </>
        ) : null}
      </header>
      {bar.shown ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '8px 24px', borderBottom: '1px solid var(--line-border)' }}>
          <span style={{ fontSize: 'var(--t-small)', whiteSpace: 'nowrap' }}>{bar.label}</span>
          <span className="mono" style={{ flex: '1 1 auto', minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>
            {bar.line}
          </span>
          <button type="button" className="btn-g" onClick={() => void model.stop()} disabled={!bar.canStop} style={{ height: 26 }}>
            Stop
          </button>
        </div>
      ) : null}
      {face.tab === 'runs' ? <RunsView model={model} /> : null}
      {face.tab === 'pipelines' ? <PipelinesView model={model} /> : null}
      {face.tab === 'cycle' ? <CycleView model={model} /> : null}
      {face.tab === 'regimes' ? <RegimesView model={model} /> : null}
      {face.tab === 'datasets' ? <DatasetsView model={model} /> : null}
    </div>
  )
}
