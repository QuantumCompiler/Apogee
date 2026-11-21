import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { SettingsViewModel } from '../ViewModels/SettingsViewModel'
import { AppearancePane } from './AppearancePane'
import { PermissionsPane, WebPane } from './AccessPanes'
import { BackendsPane } from './BackendsPane'
import { GeneralPane, KeyboardPane, MediaPane, MemoryPane, RolesPane } from './GeneralPanes'
import { XcodePane } from './XcodePane'

/**
 * The Settings window (design 1i): the old prototype's `ui.settingsOpen`
 * block (`main.html:2140-2317` at `b375fac9`) -- the panes' list beside the
 * pane shown, its title and Done, the red line under it when something could
 * not be read or a change failed, and the install at the list's foot. It
 * opens over the window as the old one does, at tokens.json's settings size.
 * Models & roles draws what the app root puts in its slot (`roles`): the
 * Models place's roles, so `settings` never imports `models`.
 */
export function SettingsView({ model, close, roles }: { readonly model: SettingsViewModel; readonly close: () => void; readonly roles?: () => ReactNode }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ position: 'absolute', inset: 0, zIndex: 60, display: 'grid', placeItems: 'center', padding: 24, background: 'var(--scrim-modal)' }}>
      <div
        role="dialog"
        aria-modal="true"
        aria-label="Settings"
        style={{ width: 'min(var(--layout-settings-width), 100%)', height: 'min(var(--layout-settings-height), 100%)', display: 'flex', overflow: 'hidden', border: '1px solid var(--line-strong)', borderRadius: 'var(--radius-dialog)', background: 'var(--surface-window)', boxShadow: 'var(--shadow-dialog)' }}
      >
        <nav aria-label="Settings sections" style={{ flex: '0 0 var(--layout-settings-nav)', display: 'flex', flexDirection: 'column', gap: 1, padding: '16px 8px', background: 'var(--surface-sidebar)', borderRight: '1px solid var(--line-border)' }}>
          <div style={{ padding: '0 10px 12px', fontSize: 'var(--t-h3)', fontWeight: 600 }}>Settings</div>
          {face.panes.map((pane) => (
            <button
              key={pane.id}
              type="button"
              className="hv"
              onClick={() => model.pick(pane.id)}
              aria-current={pane.current ? 'page' : undefined}
              style={{ display: 'flex', alignItems: 'center', height: 30, padding: '0 10px', border: 0, borderRadius: 'var(--radius-row)', background: pane.current ? 'var(--surface-selection)' : undefined, color: pane.current ? 'var(--text-primary)' : 'var(--text-secondary)', fontSize: 'var(--t-body)', fontWeight: pane.current ? 500 : 400, textAlign: 'left' }}
            >
              {pane.label}
            </button>
          ))}
          <span style={{ flex: '1 1 auto' }} />
          <p className="mono" style={{ margin: 0, padding: '0 10px', fontSize: 'var(--t-label)', lineHeight: 1.6, color: 'var(--text-tertiary)' }}>
            {face.foot.map((line, index) => (
              <span key={line}>
                {index > 0 ? <br /> : null}
                {line}
              </span>
            ))}
          </p>
        </nav>
        <div style={{ flex: '1 1 0', minWidth: 0, display: 'flex', flexDirection: 'column' }}>
          <header style={{ flex: '0 0 var(--control-header)', display: 'flex', alignItems: 'center', gap: 10, padding: '0 16px 0 28px', borderBottom: '1px solid var(--line-border)' }}>
            <h2 style={{ flex: '1 1 auto', margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>{face.title}</h2>
            <button type="button" className="btn-p" onClick={close}>
              Done
            </button>
          </header>
          <div style={{ flex: '1 1 0', minHeight: 0, overflowY: 'auto', padding: '8px 28px 32px' }}>
            {face.failure !== '' ? <p style={{ margin: '12px 0 0', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{face.failure}</p> : null}
            {face.pane === 'general' ? <GeneralPane model={model} /> : null}
            {face.pane === 'appearance' ? <AppearancePane model={model.theme} /> : null}
            {face.pane === 'roles' ? <RolesPane model={model} roles={roles} /> : null}
            {face.pane === 'media' ? <MediaPane model={model} /> : null}
            {face.pane === 'backends' ? <BackendsPane model={model} /> : null}
            {face.pane === 'permissions' ? <PermissionsPane model={model} /> : null}
            {face.pane === 'web' ? <WebPane model={model} /> : null}
            {face.pane === 'memory' ? <MemoryPane model={model} /> : null}
            {face.pane === 'keyboard' ? <KeyboardPane model={model} /> : null}
            {face.pane === 'xcode' ? <XcodePane model={model.xcode} /> : null}
          </div>
        </div>
      </div>
    </div>
  )
}
