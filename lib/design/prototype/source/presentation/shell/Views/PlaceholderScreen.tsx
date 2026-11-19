import type { Placeholder } from '../ViewModels/Destination'

/**
 * A place not yet ported (design 1e): its header, as every screen's is, and a
 * plain line naming the place and the design item that ports it -- never a
 * dead control. Each feature item removes its own.
 */
export function PlaceholderScreen({ placeholder }: { readonly placeholder: Placeholder }) {
  return (
    <>
      <header
        className="ap-tools"
        style={{ position: 'relative', zIndex: 30, flex: '0 0 var(--layout-header)', display: 'flex', alignItems: 'center', gap: 12, padding: '0 14px 0 24px', borderBottom: '1px solid var(--line-border)', background: 'var(--surface-window)' }}
      >
        <h1 style={{ margin: 0, minWidth: 0, fontSize: 'var(--t-title)', fontWeight: 600, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{placeholder.title}</h1>
      </header>
      <div className="ap-scroll" style={{ flex: '1 1 auto', minHeight: 0, display: 'grid', placeItems: 'center', padding: 24 }}>
        <p style={{ margin: 0, maxWidth: 420, textAlign: 'center', fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }}>{placeholder.says}</p>
      </div>
    </>
  )
}

/**
 * An overlay not yet ported, the console (1g) among them: the monitor's panel
 * frame over the window, its title, the line naming the item that ports it,
 * and Close.
 */
export function PlaceholderPanel({ title, says, onClose }: { readonly title: string; readonly says: string; readonly onClose: () => void }) {
  return (
    <div
      role="dialog"
      aria-label={title}
      className="menu"
      style={{ position: 'absolute', left: 'calc(var(--layout-sidebar) + 12px)', right: 16, bottom: 16, zIndex: 50, maxHeight: 'calc(100% - 32px)', display: 'flex', flexDirection: 'column', fontSize: 'var(--t-small)' }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '12px 12px 10px 18px', borderBottom: '1px solid var(--line-border)' }}>
        <h2 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>{title}</h2>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="ib" onClick={onClose} aria-label={`Close ${title.toLowerCase()}`}>
          <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
            <path d="M4.5 4.5l7 7M11.5 4.5l-7 7" />
          </svg>
        </button>
      </div>
      <p style={{ margin: 0, padding: '14px 18px', color: 'var(--text-secondary)' }}>{says}</p>
    </div>
  )
}
