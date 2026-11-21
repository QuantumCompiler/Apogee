import type { InstallCardFace, InstallCardState } from '../Media/Install'

export type { InstallCardFace, InstallCardState }

const icons: Readonly<Record<InstallCardState, { readonly color: string; readonly paths: readonly string[] }>> = {
  running: { color: 'var(--status-accent)', paths: ['M8 5v6M5.5 8.5L8 11l2.5-2.5'] },
  installed: { color: 'var(--status-accent)', paths: ['M5.5 8.2l1.8 1.8 3.2-3.6'] },
  stopped: { color: 'var(--text-tertiary)', paths: [] },
  failed: { color: 'var(--status-error)', paths: ['M8 2.5l6 10.5H2z', 'M8 6.5v3M8 11.2v.1'] },
}

/**
 * A runtime install as a card in a media place's list (design 1k: the old
 * prototype's `inst`, the Mac's `InstallCard`, promoted to `styles` for
 * Video, Compose and Voice): what it installs, the line it is on and Stop
 * while it runs; once ended, how -- installed, stopped, or failed in the
 * CLI's own words -- and Dismiss. `act` is Stop while it runs, Dismiss after.
 */
export function InstallCard({ install, act }: { readonly install: InstallCardFace; readonly act: () => void }) {
  const { state } = install
  const icon = icons[state]
  const lines = state === 'failed' ? 8 : state === 'running' ? 2 : 3
  return (
    <div className="card" role="status" style={{ marginBottom: 8, padding: 12, borderColor: state === 'failed' ? 'var(--status-error)' : 'var(--line-border)' }}>
      <div style={{ display: 'flex', alignItems: 'flex-start', gap: 8 }}>
        <svg className="i" width="14" height="14" viewBox="0 0 16 16" aria-hidden="true" style={{ marginTop: 2, color: icon.color }}>
          {state === 'failed' ? null : <circle cx="8" cy="8" r="5.5" />}
          {state === 'stopped' ? <rect x="6" y="6" width="4" height="4" rx=".5" /> : null}
          {icon.paths.map((d) => (
            <path key={d} d={d} />
          ))}
        </svg>
        <span style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-body)', fontWeight: 500 }}>{install.headline}</span>
      </div>
      {state === 'running' ? (
        <div aria-hidden="true" style={{ height: 4, marginTop: 8, borderRadius: 2, background: 'var(--line-border)', overflow: 'hidden' }}>
          <div className="pulse" style={{ width: '100%', height: '100%', background: 'var(--status-accent)' }} />
        </div>
      ) : null}
      <div className="mono" style={{ display: '-webkit-box', WebkitBoxOrient: 'vertical', WebkitLineClamp: lines, overflow: 'hidden', marginTop: 8, fontSize: 'var(--t-meta)', lineHeight: 1.5, color: state === 'failed' ? 'var(--status-error)' : 'var(--text-tertiary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
        {install.detail}
      </div>
      <div className="mono" style={{ marginTop: 6, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
        {install.command}
      </div>
      <div style={{ marginTop: 8 }}>
        <button type="button" className="btn-g" onClick={act} style={{ height: 24, fontSize: 'var(--t-small)' }}>
          {state === 'running' ? 'Stop' : 'Dismiss'}
        </button>
      </div>
    </div>
  )
}
