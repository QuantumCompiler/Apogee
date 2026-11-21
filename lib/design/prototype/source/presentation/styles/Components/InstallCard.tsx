import type { IconName } from '../Icons/IconNames'
import type { InstallCardFace, InstallCardState } from '../Media/Install'
import { Icon } from './Icon'

export type { InstallCardFace, InstallCardState }

/** Each state's icon, from the set (design 1q), and its color. */
const icons: Readonly<Record<InstallCardState, { readonly color: string; readonly name: IconName }>> = {
  running: { color: 'var(--status-accent)', name: 'installing' },
  installed: { color: 'var(--status-accent)', name: 'installed' },
  stopped: { color: 'var(--text-tertiary)', name: 'installStopped' },
  failed: { color: 'var(--status-error)', name: 'error' },
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
        <Icon name={icon.name} size={14} style={{ marginTop: 2, color: icon.color }} />
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
