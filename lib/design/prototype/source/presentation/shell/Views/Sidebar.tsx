import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import { destinations, titleOf } from '../ViewModels/Destination'
import type { Destination } from '../ViewModels/Destination'
import { keyOf, symbols } from '../ViewModels/KeyMap'
import type { ShellViewModel } from '../ViewModels/ShellViewModel'
import type { SystemStrip } from '../ViewModels/SystemStrip'

/** Each place's icon, the old prototype's (`main.html:133-143` at `b375fac9`). */
const icons: Readonly<Record<Destination, ReactNode>> = {
  sessions: <path d="M3 4.5A1.5 1.5 0 0 1 4.5 3h7A1.5 1.5 0 0 1 13 4.5v5a1.5 1.5 0 0 1-1.5 1.5H7.5L4.5 13.5V11A1.5 1.5 0 0 1 3 9.5z" />,
  models: (
    <>
      <path d="M8 2l5.5 3v6L8 14l-5.5-3V5z" />
      <path d="M2.5 5L8 8l5.5-3M8 8v6" />
    </>
  ),
  suites: (
    <>
      <path d="M8 2.5l5.5 3L8 8.5l-5.5-3z" />
      <path d="M2.5 8.5L8 11.5l5.5-3" />
      <path d="M2.5 11L8 14l5.5-3" />
    </>
  ),
  tasks: (
    <>
      <path d="M2.5 4.5l1.2 1.2L6 3.5M2.5 10l1.2 1.2L6 9" />
      <path d="M8.5 4.8h5M8.5 10.3h5" />
    </>
  ),
  graph: (
    <>
      <circle cx="4" cy="4" r="1.5" />
      <circle cx="12" cy="6" r="1.5" />
      <circle cx="6" cy="12" r="1.5" />
      <path d="M5.4 4.4l5.2 1.2M4.4 5.4l1.2 5.1M10.9 7.1L7.1 11" />
    </>
  ),
  video: (
    <>
      <rect x="1.75" y="4" width="9" height="8" rx="1.5" />
      <path d="M10.75 7.2L14.25 5v6l-3.5-2.2" />
    </>
  ),
  voice: <path d="M2 7v2M4.75 5v6M7.5 2.5v11M10.25 5v6M13 6.5v3" />,
  knowledge: (
    <>
      <path d="M3 2.5h3v11H3zM7 2.5h3v11H7z" />
      <path d="M10.6 3.2l2.5.7-2.5 9.4-2.5-.7z" />
    </>
  ),
  training: (
    <>
      <path d="M1.5 6L8 3l6.5 3L8 9z" />
      <path d="M4 7.3v3.4C5.5 12 10.5 12 12 10.7V7.3M14.5 6v4" />
    </>
  ),
  library: (
    <>
      <path d="M2.5 3.5h4v4h-4zM9.5 3.5h4v4h-4zM2.5 9.5h4v4h-4z" />
      <path d="M11.5 9.5v4M9.5 11.5h4" />
    </>
  ),
  review: (
    <>
      <circle cx="4.5" cy="3.5" r="1.5" />
      <circle cx="4.5" cy="12.5" r="1.5" />
      <circle cx="11.5" cy="12.5" r="1.5" />
      <path d="M4.5 5v6M11.5 11V7a2 2 0 0 0-2-2H7" />
      <path d="M8.5 3.5L7 5l1.5 1.5" />
    </>
  ),
}

export interface SidebarProps {
  readonly model: ShellViewModel
  /** The SYSTEM strip's numbers, the monitor's, handed in by `app`; `undefined` before anything was read. */
  readonly strip?: SystemStrip | undefined
  /** The session list's slot: the Sessions place's list (1g), handed in by `app`. */
  readonly sessions?: (() => ReactNode) | undefined
  /** The top row's new-session buttons (1g), handed in by `app`. */
  readonly newSessions?: (() => ReactNode) | undefined
  /** Runs going on in a place, by place: its badge. */
  readonly running?: Readonly<Partial<Record<Destination, number>>> | undefined
}

/**
 * The sidebar (design 1e): the old prototype's (`main.html:81-170` at
 * `b375fac9`) -- the window's top row with its new-session buttons, the
 * session list, the eleven places with their running badges, the SYSTEM
 * strip with the monitor's and the console's buttons, Settings and the theme
 * button. Below 900 px the window's `data-collapsed` makes it a 56-px column
 * of icons. The new-session buttons and the list are the Sessions place's
 * (1g), handed in by `app`, as the shell imports no place.
 */
export function Sidebar({ model, strip, sessions, newSessions, running = {} }: SidebarProps) {
  useObserved(model)
  useObserved(model.theme)
  const watching = model.overlay === 'monitor'
  const typing = model.overlay === 'console'
  return (
    <aside
      className="ap-side"
      aria-label="Sidebar"
      style={{ flex: '0 0 var(--layout-sidebar)', minWidth: 0, minHeight: 0, display: 'flex', flexDirection: 'column', background: 'var(--surface-sidebar)', borderRight: '1px solid var(--line-border)' }}
    >
      <div style={{ flex: '0 0 var(--layout-header)', display: 'flex', alignItems: 'center', gap: 8, padding: '0 10px 0 18px' }}>
        <span className="ap-lights" aria-hidden="true" style={{ display: 'flex', gap: 8 }}>
          {['close', 'minimize', 'zoom'].map((light) => (
            <span key={light} style={{ width: 12, height: 12, borderRadius: 'var(--radius-dot)', background: `var(--window-${light})` }} />
          ))}
        </span>
        <span style={{ flex: '1 1 auto' }} />
        {newSessions?.()}
      </div>

      <nav className="ap-list" aria-label="Sessions" style={{ flex: '1 1 auto', minHeight: 0, overflowY: 'auto', padding: '0 8px 10px' }}>
        {sessions?.()}
      </nav>

      <nav className="ap-places" aria-label="Places" style={{ flex: '0 0 auto', padding: '6px 8px 4px', borderTop: '1px solid var(--line-border)' }}>
        <div className="ap-places-row" style={{ display: 'flex', gap: 2, overflowX: 'auto', overscrollBehaviorX: 'contain', scrollSnapType: 'x proximity', paddingBottom: 2 }}>
          {destinations.map((destination) => {
            const current = model.route === destination
            const runs = running[destination] ?? 0
            const label = `${titleOf(destination)} ${symbols(keyOf(destination))}${runs > 0 ? ` · ${runs} running` : ''}`
            return (
              <button
                key={destination}
                type="button"
                className="ib"
                onClick={() => model.go(destination)}
                aria-current={current ? 'page' : undefined}
                aria-label={label}
                title={label}
                style={{
                  position: 'relative',
                  flex: '0 0 auto',
                  width: 40,
                  height: 34,
                  scrollSnapAlign: 'start',
                  background: current ? 'var(--surface-selection)' : undefined,
                  color: current ? 'var(--text-primary)' : 'var(--text-secondary)',
                }}
              >
                <svg className="i" width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">
                  {icons[destination]}
                </svg>
                {runs > 0 ? (
                  <span
                    aria-hidden="true"
                    style={{ position: 'absolute', top: 6, right: 8, width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: 'var(--status-accent)', boxShadow: '0 0 0 2px var(--surface-sidebar)' }}
                  />
                ) : null}
              </button>
            )
          })}
        </div>
      </nav>

      <section className="ap-strip" aria-label="System" style={{ flex: '0 0 auto', padding: '6px 8px', borderTop: '1px solid var(--line-border)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 2 }}>
          <button
            type="button"
            className="hv"
            onClick={() => model.toggleSystem()}
            aria-expanded={model.systemOpen}
            style={{ flex: '1 1 auto', minWidth: 0, display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: '0 8px', border: 0, borderRadius: 'var(--radius-row)', background: 'transparent', color: 'var(--text-tertiary)', textAlign: 'left' }}
          >
            <svg className="i" width="10" height="10" viewBox="0 0 16 16" aria-hidden="true" style={{ transform: `rotate(${model.systemOpen ? 0 : -90}deg)`, transition: 'transform var(--motion-chevron)' }}>
              <path d="M4 6l4 4 4-4" />
            </svg>
            <span className="lbl">SYSTEM</span>
            {!model.systemOpen && strip ? (
              <span
                className="mono"
                style={{ flex: '1 1 auto', minWidth: 0, marginLeft: 4, fontSize: 'var(--t-meta)', color: strip.warning ? 'var(--status-warning)' : 'var(--text-secondary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}
              >
                {strip.summary}
              </span>
            ) : null}
          </button>
          <button
            type="button"
            className="ib"
            onClick={() => model.perform({ kind: 'toggleMonitor' })}
            aria-label="System monitor (Option Command M)"
            aria-expanded={watching}
            title="System monitor ⌥⌘M"
            style={{ width: 26, height: 26, background: watching ? 'var(--surface-selection)' : undefined, color: 'var(--text-tertiary)' }}
          >
            <svg className="i" width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
              <path d="M1.5 8.5h3l1.5-4 3 8 1.5-4h4" />
            </svg>
          </button>
          <button
            type="button"
            className="ib"
            onClick={() => model.perform({ kind: 'toggleConsole' })}
            aria-label="Console (Shift Command K)"
            aria-expanded={typing}
            title="Console ⇧⌘K"
            style={{ width: 26, height: 26, background: typing ? 'var(--surface-selection)' : undefined, color: 'var(--text-tertiary)' }}
          >
            <svg className="i" width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
              <path d="M3 4.5l3.5 3.5L3 11.5M8 12h5" />
            </svg>
          </button>
        </div>
        {model.systemOpen ? (
          <button
            type="button"
            className="hv"
            onClick={() => model.perform({ kind: 'toggleMonitor' })}
            aria-label="Open the system monitor"
            style={{ width: '100%', display: 'grid', gridTemplateColumns: '46px minmax(0, 1fr) auto', gap: '6px 10px', alignItems: 'center', padding: 6, paddingLeft: 8, paddingRight: 8, marginTop: 2, border: 0, borderRadius: 'var(--radius-control)', background: 'transparent', color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)', fontSize: 'var(--t-meta)', lineHeight: '16px', textAlign: 'left' }}
          >
            {strip === undefined ? (
              <span style={{ gridColumn: '1 / -1', color: 'var(--text-tertiary)' }}>Not read yet.</span>
            ) : strip.rows.length === 0 ? (
              <span style={{ gridColumn: '1 / -1', minWidth: 0, color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{strip.summary}</span>
            ) : (
              strip.rows.map((row) => [
                <span key={`${row.label}-label`} style={{ color: 'var(--text-tertiary)' }}>
                  {row.label}
                </span>,
                row.percent === undefined ? null : (
                  <span key={`${row.label}-bar`} style={{ display: 'block', height: 4, borderRadius: 2, background: 'var(--line-border)', overflow: 'hidden' }}>
                    <span style={{ display: 'block', width: `${row.percent}%`, height: '100%', background: row.warning ? 'var(--status-warning)' : 'var(--text-secondary)' }} />
                  </span>
                ),
                <span key={`${row.label}-value`} style={row.percent === undefined ? { gridColumn: '2 / -1' } : undefined}>
                  {row.value}
                </span>,
              ])
            )}
          </button>
        ) : null}
      </section>

      <div style={{ flex: '0 0 auto', display: 'flex', alignItems: 'center', gap: 4, padding: '6px 8px 12px', borderTop: '1px solid var(--line-border)' }}>
        <button
          type="button"
          className="hv"
          onClick={() => model.perform({ kind: 'openSettings' })}
          aria-label="Settings (Command comma)"
          style={{ flex: '1 1 auto', display: 'flex', alignItems: 'center', gap: 10, height: 30, padding: '0 10px', border: 0, borderRadius: 'var(--radius-control)', background: 'transparent', color: 'var(--text-secondary)', fontSize: 'var(--t-dense)', textAlign: 'left' }}
        >
          <svg className="i" width="15" height="15" viewBox="0 0 16 16" aria-hidden="true">
            <circle cx="8" cy="8" r="2.2" />
            <path d="M8 1.8v1.8M8 12.4v1.8M14.2 8h-1.8M3.6 8H1.8M12.4 3.6l-1.3 1.3M4.9 11.1l-1.3 1.3M12.4 12.4l-1.3-1.3M4.9 4.9L3.6 3.6" />
          </svg>
          <span className="ap-lab" style={{ flex: '1 1 auto' }}>
            Settings
          </span>
          <span className="mono ap-lab" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            ⌘,
          </span>
        </button>
        <button type="button" className="ib ap-lab" onClick={() => model.cycleTheme()} aria-label={`Theme: ${model.themeName}. Switch theme`} title={`Theme: ${model.themeName}`}>
          <svg width="15" height="15" viewBox="0 0 16 16" aria-hidden="true">
            <circle cx="8" cy="8" r="5.5" fill="none" stroke="currentColor" strokeWidth="1.5" />
            <path d="M8 2.5a5.5 5.5 0 0 1 0 11z" fill="currentColor" />
          </svg>
        </button>
      </div>
    </aside>
  )
}
