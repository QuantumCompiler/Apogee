import { useObserved } from '../../styles/Observation/useObserved'
import type { MonitorViewModel } from '../ViewModels/MonitorViewModel'

const label = { color: 'var(--text-secondary)' } as const
const unknownValue = { color: 'var(--text-tertiary)', opacity: 0.75 } as const

/**
 * The system monitor, over the window (design 1e): the old prototype's panel
 * (`main.html:2090-2125` at `b375fac9`), its regions, states, controls and
 * words, every number from the `system` reading the view-model holds. Where
 * the CLI reports no value the panel says so: the GPU is its name and model
 * budget, not a percent, and the store is its one total. Its stale line is
 * drawn at full opacity: the old panel faded only the GPU's history bars,
 * which this one has none of. With no model held, it says so in the CLI's
 * words: a one-shot `apogee system` holds none, whatever the sessions hold.
 */
export function MonitorView({ model, close }: { readonly model: MonitorViewModel; readonly close: () => void }) {
  useObserved(model)
  const face = model.face()
  return (
    <div
      role="dialog"
      aria-label="System monitor"
      className="menu"
      style={{ position: 'absolute', left: 'calc(var(--layout-sidebar) + 12px)', bottom: 16, zIndex: 50, width: 'var(--layout-monitor)', maxWidth: 'calc(100% - var(--layout-sidebar) - 34px)', maxHeight: 'calc(100% - 32px)', overflowY: 'auto', fontSize: 'var(--t-small)' }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '12px 12px 10px 18px', borderBottom: '1px solid var(--line-border)' }}>
        <h2 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>System</h2>
        <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          updates every 2 s · ⌥⌘M
        </span>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="ib" onClick={close} aria-label="Close system monitor">
          <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
            <path d="M4.5 4.5l7 7M11.5 4.5l-7 7" />
          </svg>
        </button>
      </div>

      {face.state === 'failed' ? (
        <section style={{ padding: '14px 18px' }}>
          <p role="alert" style={{ margin: 0, color: 'var(--status-error)', overflowWrap: 'anywhere' }}>
            {face.failure}
          </p>
        </section>
      ) : face.state === 'waiting' ? (
        <section style={{ padding: '14px 18px' }}>
          <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>Reading the system…</p>
        </section>
      ) : (
        <>
          <section style={{ padding: '14px 18px', borderBottom: '1px solid var(--line-border)' }}>
            <h3 className="lbl" style={{ margin: '0 0 10px' }}>
              {face.machine}
            </h3>
            <div style={{ display: 'grid', gridTemplateColumns: '70px 64px minmax(0, 1fr)', gap: '10px 12px', alignItems: 'center' }}>
              <span style={label}>CPU</span>
              <span className="mono" style={{ fontSize: 'var(--t-title)' }}>
                {face.cpu.value}
              </span>
              <span data-history="" aria-hidden="true" style={{ display: 'flex', alignItems: 'flex-end', gap: 2, height: 26 }}>
                {face.cpu.bars.map((height, index) => (
                  <span key={index} style={{ flex: '1 1 0', height: `${height}%`, minHeight: 1, borderRadius: 1, background: 'var(--text-tertiary)' }} />
                ))}
              </span>

              <span style={label}>GPU</span>
              <span style={{ gridColumn: '2 / -1', display: 'flex', flexDirection: 'column', gap: 3 }}>
                <span className="mono" style={{ fontSize: 'var(--t-title)', color: face.gpu.stale ? 'var(--text-tertiary)' : 'var(--text-primary)' }}>
                  {face.gpu.value}
                </span>
                <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>
                  {face.gpu.detail}
                </span>
                {face.gpu.stale ? (
                  <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                    {face.gpu.stale}
                  </span>
                ) : null}
              </span>

              <span style={label}>Memory</span>
              <span className="mono" style={{ fontSize: 'var(--t-title)', color: face.memory.warning ? 'var(--status-warning)' : 'var(--text-secondary)' }}>
                {face.memory.value}
              </span>
              <span style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                <span aria-hidden="true" style={{ display: 'flex', gap: 2, height: 8, borderRadius: 3, overflow: 'hidden' }}>
                  <span style={{ flex: `${face.memory.shares.apogee} 1 0`, background: 'var(--status-accent)' }} />
                  <span style={{ flex: `${face.memory.shares.others} 1 0`, background: 'var(--text-tertiary)' }} />
                  <span style={{ flex: `${face.memory.shares.free} 1 0`, background: 'var(--line-border)' }} />
                </span>
                <span className="mono" style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 12px', fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>
                  <span>
                    <span style={{ color: 'var(--status-accent)' }}>■</span> Apogee {face.memory.apogee}
                  </span>
                  <span>
                    <span style={{ color: 'var(--text-tertiary)' }}>■</span> other apps {face.memory.others}
                  </span>
                  <span>free {face.memory.free}</span>
                </span>
              </span>

              <span style={label}>Temperature</span>
              <span className="mono" style={unknownValue}>
                unknown
              </span>
              <span style={{ color: 'var(--text-tertiary)' }}>macOS doesn’t report it to apps without a helper</span>

              <span style={label}>Power</span>
              <span className="mono" style={unknownValue}>
                unknown
              </span>
              <span style={{ color: 'var(--text-tertiary)' }}>not readable without administrator access</span>
            </div>
          </section>

          <section style={{ padding: '14px 18px', borderBottom: '1px solid var(--line-border)' }}>
            <h3 className="lbl" style={{ margin: '0 0 10px', display: 'flex', justifyContent: 'space-between' }}>
              LOADED MODELS<span style={{ letterSpacing: 0 }}>{face.models.total}</span>
            </h3>
            {face.models.rows.map((row) => (
              <div key={row.name} style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto auto', gap: 12, alignItems: 'center', padding: '8px 0', borderBottom: '1px solid var(--line-border)' }}>
                <div style={{ minWidth: 0 }}>
                  <div style={{ fontSize: 'var(--t-body)' }}>{row.name}</div>
                  <div className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                    {row.detail}
                  </div>
                </div>
                <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
                  {row.size}
                </span>
                <button type="button" className="btn-g" onClick={() => model.unloadModel(row.name)} style={{ height: 26, fontSize: 'var(--t-small)' }}>
                  {row.action}
                </button>
              </div>
            ))}
            {face.models.empty ? (
              <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>
                None by this process -- a session holds its own. A model loads inside the session that needs it, and unloading one ends that session’s process until its next message.
              </p>
            ) : null}
          </section>

          <section style={{ padding: '14px 18px' }}>
            <h3 className="lbl" style={{ margin: '0 0 10px', display: 'flex', justifyContent: 'space-between', gap: 12 }}>
              <span style={{ minWidth: 0, overflowWrap: 'anywhere' }}>{face.store.heading}</span>
              <span style={{ letterSpacing: 0, whiteSpace: 'nowrap' }}>{face.store.total}</span>
            </h3>
            {face.store.rows.map((row) => (
              <div key={row.label} style={{ display: 'grid', gridTemplateColumns: '110px minmax(0, 1fr) 70px', gap: 10, alignItems: 'center', marginBottom: 6 }}>
                <span style={label}>{row.label}</span>
                <span style={{ height: 5, borderRadius: 3, background: 'var(--line-border)', overflow: 'hidden' }}>
                  <span style={{ display: 'block', width: `${row.percent}%`, height: '100%', background: 'var(--text-secondary)' }} />
                </span>
                <span className="mono" style={{ fontSize: 'var(--t-ui)', textAlign: 'right' }}>
                  {row.size}
                </span>
              </div>
            ))}
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginTop: 10 }}>
              <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                {face.store.free}
              </span>
              <button type="button" className="btn-g" onClick={() => model.showLibrary()} style={{ height: 26, fontSize: 'var(--t-small)' }}>
                Open models library
              </button>
            </div>
          </section>
        </>
      )}
    </div>
  )
}
