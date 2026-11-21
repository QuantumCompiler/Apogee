import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VideoViewModel } from '../ViewModels/VideoViewModel'
import { ClipPage, RunCard } from './ClipPage'
import { ClipsList } from './ClipsList'
import { GenerateForm } from './GenerateForm'

/**
 * The Video place (design 1k): the old prototype's screen (`main.html` at
 * `b375fac9`, `ui.isVideo`) -- its header with the two tabs, the default
 * pipeline and New clip, then Clips: the list (the install card, the run, the
 * clips), the run or a clip's page, and the generate form. Like the Mac's
 * `VideoScreen<Compositions>`, it takes the compositions tab as a slot the
 * app root fills (1l's).
 */
export function VideoScreen({ model, compositions }: { readonly model: VideoViewModel; readonly compositions: () => ReactNode }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 16, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Video</h1>
        <div role="tablist" aria-label="Show" className="seg">
          {face.tabs.map((tab) => (
            <button key={tab.tab} type="button" role="tab" aria-selected={tab.current} onClick={() => model.pick(tab.tab)} style={{ display: 'inline-flex', alignItems: 'center', gap: 6, background: tab.current ? 'var(--surface-control)' : 'transparent', color: tab.current ? 'var(--text-primary)' : 'var(--text-secondary)', fontWeight: tab.current ? 600 : 400 }}>
              {tab.label}
              {tab.count === '' ? null : (
                <span className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
                  {tab.count}
                </span>
              )}
            </button>
          ))}
        </div>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="hv" onClick={() => model.openDefault()} title="models.default_video — show it in the library" style={{ display: 'inline-flex', alignItems: 'center', gap: 7, height: 28, padding: '0 8px', border: 0, borderRadius: 6, background: 'transparent', color: 'var(--text-tertiary)', fontSize: 'var(--t-small)', whiteSpace: 'nowrap' }}>
          <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true" style={{ color: 'var(--model-local)' }}>
            <rect x="4" y="4" width="8" height="8" rx="1.5" />
            <path d="M6.5 2v2M9.5 2v2M6.5 12v2M9.5 12v2M2 6.5h2M2 9.5h2M12 6.5h2M12 9.5h2" />
          </svg>
          {face.pipelineLine}
        </button>
        <button type="button" className="btn-p" onClick={() => model.newPrimary()} disabled={!face.canNew}>
          <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
            <path d="M8 3.5v9M3.5 8h9" />
          </svg>
          {face.newLabel}
        </button>
      </header>
      {face.tab === 'clips' ? (
        <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
          <ClipsList model={model} />
          <div style={{ flex: '1 1 0', minWidth: 0, minHeight: 0, overflowY: 'auto', padding: '20px 28px 32px' }}>
            <div style={{ maxWidth: 820, display: 'flex', flexDirection: 'column', gap: 16 }}>
              {face.showRun ? <RunCard model={model} /> : null}
              {face.clip.shown ? <ClipPage model={model} /> : null}
            </div>
          </div>
          <GenerateForm model={model} />
        </div>
      ) : (
        compositions()
      )}
    </div>
  )
}

/** The compositions slot in Video's own previews, which may not import `compose`: a plain placeholder, as the Mac's previews draw "The compositions". The app root fills the slot with the compose module's view (1l). */
export function CompositionsPlaceholder() {
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'grid', placeItems: 'center', padding: 24, color: 'var(--text-tertiary)', fontSize: 'var(--t-small)' }}>
      <p style={{ margin: 0 }}>The compositions. The app draws them here.</p>
    </div>
  )
}
