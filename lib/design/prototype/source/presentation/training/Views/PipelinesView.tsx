import { useObserved } from '../../styles/Observation/useObserved'
import type { StageRowFace } from '../ViewModels/TrainingFaces'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'

const toneColor: Readonly<Record<StageRowFace['tone'], string>> = {
  passed: 'var(--status-accent)',
  failed: 'var(--status-error)',
  pending: 'var(--text-tertiary)',
  active: 'var(--text-primary)',
}

/** A pipeline's stages, each its index, name, line and run: live from a stream, or a manifest's (the old `trStageRows`). */
export function StageList({ stages }: { readonly stages: readonly StageRowFace[] }) {
  return stages.map((stage) => (
    <div key={stage.index} style={{ display: 'grid', gridTemplateColumns: '18px minmax(0, 1fr)', gap: '0 12px', alignItems: 'baseline', padding: '6px 0' }}>
      <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', textAlign: 'right' }}>
        {stage.index}
      </span>
      <div>
        <div>{stage.name}</div>
        <div className="mono" style={{ fontSize: 'var(--t-meta)', color: toneColor[stage.tone] }}>
          {stage.line}
        </div>
        {stage.runLine !== '' ? (
          <div className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            {stage.runLine}
          </div>
        ) : null}
      </div>
    </div>
  ))
}

const rowStyle = { width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '10px 12px', marginBottom: 2, border: 0, borderRadius: 7, textAlign: 'left', color: 'var(--text-primary)' } as const
const quiet = { margin: '2px 12px 0', fontSize: 'var(--t-meta)', lineHeight: 1.5, color: 'var(--text-tertiary)', overflowWrap: 'anywhere' } as const

/** Pipelines (design 1j): the named pipelines, the runs, and one run's page -- the one running here live, or a manifest -- the old `trn.plOn` block. */
export function PipelinesView({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const face = model.face().pipelines
  const page = face.page
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <nav aria-label="Pipelines" style={{ flex: '0 0 320px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', background: 'var(--surface-sidebar)', borderRight: '1px solid var(--line-border)' }}>
        <h3 className="lbl" style={{ margin: '6px 12px 6px' }}>
          NAMED PIPELINES
        </h3>
        {face.specs.map((spec) => (
          <div key={spec.name} style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '8px 12px' }}>
            <div style={{ flex: '1 1 auto', minWidth: 0 }}>
              <div>{spec.name}</div>
              <div style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{spec.meta}</div>
            </div>
            <button type="button" className="btn-g" onClick={() => model.requestRun(spec.name)} disabled={!spec.canRun} aria-label={spec.label} title={spec.command}>
              Run…
            </button>
          </div>
        ))}
        {face.noSpecs !== '' ? (
          <p className="mono" style={quiet}>
            {face.noSpecs}
          </p>
        ) : null}
        <h3 className="lbl" style={{ margin: '22px 12px 6px' }}>
          RUNS
        </h3>
        {face.live.shown ? (
          <button type="button" className="hv" onClick={() => void model.openPipelineRun(undefined)} aria-current={face.live.current} style={{ ...rowStyle, background: face.live.current ? 'var(--surface-selection)' : '' }}>
            <span className="mono">{face.live.id}</span>
            <span style={{ fontSize: 'var(--t-meta)', color: 'var(--status-accent)' }}>running here</span>
          </button>
        ) : null}
        {face.rows.map((row) => (
          <button key={row.id} type="button" className="hv" onClick={() => void model.openPipelineRun(row.id)} aria-current={row.current} style={{ ...rowStyle, background: row.current ? 'var(--surface-selection)' : '' }}>
            <span className="mono">{row.id}</span>
            <span style={{ maxWidth: '100%', fontSize: 'var(--t-meta)', color: row.failed ? 'var(--status-error)' : 'var(--text-tertiary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{row.line}</span>
          </button>
        ))}
        {face.noRuns !== '' ? (
          <p className="mono" style={quiet}>
            {face.noRuns}
          </p>
        ) : null}
        {face.failure !== '' ? <p style={{ ...quiet, color: 'var(--status-error)' }}>{face.failure}</p> : null}
      </nav>
      <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '22px 28px 32px', background: 'var(--surface-inspector)' }}>
        <div style={{ maxWidth: 760 }}>
          {page.kind === 'live' ? (
            <>
              <h2 className="mono" style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>
                {page.id}
              </h2>
              <p style={{ margin: '6px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>Running here, each stage as its manifest moves -- Stop is above; what passed stays.</p>
            </>
          ) : null}
          {page.kind === 'run' ? (
            <>
              <h2 className="mono" style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>
                {page.id}
              </h2>
              <div style={{ marginTop: 6, fontSize: 'var(--t-small)', color: page.failed ? 'var(--status-error)' : 'var(--text-secondary)' }}>{page.line}</div>
              <div className="mono" style={{ marginTop: 4, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                {page.when}
              </div>
            </>
          ) : null}
          {page.kind !== 'none' ? (
            <>
              <h3 className="lbl" style={{ margin: '20px 0 6px' }}>
                STAGES
              </h3>
              <StageList stages={page.stages} />
              {page.noStages ? <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>No stage reported yet.</p> : null}
            </>
          ) : null}
          {page.kind === 'run' ? (
            <>
              {page.resume.shown ? (
                <>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginTop: 18 }}>
                    <span style={{ flex: '1 1 auto', fontSize: 'var(--t-small)', overflowWrap: 'anywhere' }}>{page.resume.line}</span>
                    <button type="button" className="btn-g" onClick={() => model.requestResume()} disabled={!page.resume.enabled} title={page.resume.command}>
                      Resume…
                    </button>
                  </div>
                  {page.resume.hold !== '' ? (
                    <p role="status" style={{ margin: '6px 0 0', fontSize: 'var(--t-small)', color: 'var(--status-warning)', overflowWrap: 'anywhere', userSelect: 'text' }}>
                      {page.resume.hold}
                    </p>
                  ) : null}
                </>
              ) : null}
              <p style={{ margin: '18px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{page.endLine}</p>
              {page.isCyclePass ? (
                <button type="button" className="btn-g" onClick={() => model.showCycle()} title="The Cycle tab: its passes, and Run one pass" style={{ marginTop: 10 }}>
                  Show the cycle
                </button>
              ) : null}
            </>
          ) : null}
          {page.kind === 'none' ? (
            page.failure !== '' ? (
              <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{page.failure}</p>
            ) : (
              <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Choose a pipeline run to see its stages.</p>
            )
          ) : null}
        </div>
      </div>
    </div>
  )
}
