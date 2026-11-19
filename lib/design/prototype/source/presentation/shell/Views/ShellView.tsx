import { useEffect } from 'react'
import type { CSSProperties, ReactNode } from 'react'
import { rootStyle } from '../../styles/Appearance/Appearance'
import { useObserved } from '../../styles/Observation/useObserved'
import { titleOf } from '../ViewModels/Destination'
import type { Destination, Overlay } from '../ViewModels/Destination'
import { chordOf } from '../ViewModels/KeyMap'
import type { ShellViewModel } from '../ViewModels/ShellViewModel'
import type { SystemStrip } from '../ViewModels/SystemStrip'
import { MenuScrim, ToastHost } from './Hosts'
import { Sidebar } from './Sidebar'

export interface ShellViewProps {
  readonly model: ShellViewModel
  /** Each place's screen: `app` hands it in, so the shell never imports a place (`ShellView.swift:13-25`). */
  readonly screen: (destination: Destination) => ReactNode
  /** What each overlay draws: the monitor, the console. */
  readonly overlay: (overlay: Overlay) => ReactNode
  /** Settings, open over the window. */
  readonly settings?: () => ReactNode
  /** The open sheet's content, by kind: its place's, in the sheet host. */
  readonly sheet?: (kind: string) => ReactNode
  /** The asking confirm's content, by kind: its place's, in the confirm host. */
  readonly confirm?: (kind: string) => ReactNode
  /** The sidebar's session list (1g). */
  readonly sessions?: () => ReactNode
  /** The sidebar's top row's new-session buttons (1g). */
  readonly newSessions?: () => ReactNode
  /** The SYSTEM strip's numbers. */
  readonly strip?: SystemStrip | undefined
  /** Runs going on, by place: the places' badges. */
  readonly running?: Readonly<Partial<Record<Destination, number>>>
}

/**
 * The window around every screen (design 1e): the old prototype's root,
 * `<div class="ap">` (`main.html:79` at `b375fac9`), its theme and variables
 * from the theme service -- `data-theme` for the generated theme block,
 * `rootVars()` inline -- the sidebar, the place shown, and over them the
 * menu scrim, the overlay, Settings, the sheet and confirm hosts and the
 * toast. It hands every key the browser passes to the shell's key map, and
 * tells the shell its width, which collapses the sidebar at 900 px.
 */
export function ShellView({ model, screen, overlay, settings, sheet, confirm, sessions, newSessions, strip, running }: ShellViewProps) {
  useObserved(model)
  useObserved(model.theme)

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (model.press(chordOf(event))) event.preventDefault()
    }
    const onResize = () => model.windowResized(window.innerWidth)
    onResize()
    window.addEventListener('keydown', onKey)
    window.addEventListener('resize', onResize)
    return () => {
      window.removeEventListener('keydown', onKey)
      window.removeEventListener('resize', onResize)
    }
  }, [model])

  const layout = model.layout
  const style = {
    ...rootStyle(model.theme.appearance),
    height: '100vh',
    minHeight: 'var(--layout-window-min-height)',
    display: 'flex',
    position: 'relative',
    overflow: 'hidden',
    background: 'var(--surface-window)',
    color: 'var(--text-primary)',
    fontFamily: 'var(--font-ui)',
    fontSize: 'var(--t-body)',
    lineHeight: 1.5,
    WebkitFontSmoothing: 'antialiased',
  } as CSSProperties
  return (
    <div className="ap" data-theme={model.theme.theme} data-collapsed={layout.collapsed ? '' : undefined} data-narrow={layout.narrow ? '' : undefined} style={style}>
      {model.sidebarHidden ? null : <Sidebar model={model} strip={strip} sessions={sessions} newSessions={newSessions} running={running} />}
      <main aria-label={titleOf(model.route)} style={{ flex: '1 1 0', minWidth: 0, minHeight: 0, display: 'flex', flexDirection: 'column', position: 'relative' }}>
        {screen(model.route)}
      </main>
      {model.menuOpen ? <MenuScrim onClose={() => model.closeMenus()} /> : null}
      {model.overlay ? overlay(model.overlay) : null}
      {model.settingsOpen ? settings?.() : null}
      {model.sheet ? sheet?.(model.sheet) : null}
      {model.confirm ? confirm?.(model.confirm) : null}
      {model.toast ? <ToastHost key={model.toast.id} text={model.toast.text} /> : null}
    </div>
  )
}
