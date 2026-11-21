import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import { destinations, titleOf } from '../ViewModels/Destination'
import type { Destination } from '../ViewModels/Destination'
import { keyOf, symbols } from '../ViewModels/KeyMap'
import type { ShellViewModel } from '../ViewModels/ShellViewModel'
import type { SystemStrip } from '../ViewModels/SystemStrip'
import { Icon } from '../../styles/Components/Icon'
import type { IconName } from '../../styles/Icons/IconNames'

/** Each place's icon, the old prototype's (`main.html:133-143` at `b375fac9`), from `styles`' set (design 1q). */
const icons: Readonly<Record<Destination, IconName>> = {
  sessions: 'sessions',
  models: 'models',
  suites: 'suites',
  tasks: 'tasks',
  graph: 'graph',
  video: 'camera',
  voice: 'voice',
  knowledge: 'knowledge',
  training: 'training',
  library: 'library',
  review: 'review',
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
                  width: 'var(--layout-place-width)',
                  height: 'var(--layout-place-height)',
                  scrollSnapAlign: 'start',
                  background: current ? 'var(--surface-selection)' : undefined,
                  color: current ? 'var(--text-primary)' : 'var(--text-secondary)',
                }}
              >
                <Icon name={icons[destination]} size={16} />
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
            <Icon name="disclosure" size={10} style={{ transform: `rotate(${model.systemOpen ? 0 : -90}deg)`, transition: 'transform var(--motion-chevron)' }} />
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
            <Icon name="monitor" size={14} />
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
            <Icon name="console" size={14} />
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
          <Icon name="settings" size={15} />
          <span className="ap-lab" style={{ flex: '1 1 auto' }}>
            Settings
          </span>
          <span className="mono ap-lab" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            ⌘,
          </span>
        </button>
        <button type="button" className="ib ap-lab" onClick={() => model.cycleTheme()} aria-label={`Theme: ${model.themeName}. Switch theme`} title={`Theme: ${model.themeName}`}>
          <Icon name="theme" size={15} />
        </button>
      </div>
    </aside>
  )
}
