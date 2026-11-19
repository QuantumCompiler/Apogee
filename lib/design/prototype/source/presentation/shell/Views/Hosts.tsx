import type { ReactNode } from 'react'

/**
 * The hosts the shell opens over the window (design 1e), the old prototype's
 * frames at `b375fac9`: the menu scrim (`main.html:2088`), the sheet host
 * (`ui.gsOpen`, 2466-2497), the confirm host (`ui.confirmOpen`, 2500-2507)
 * and the toast (2509). Each frame is the shell's; what a sheet or a confirm
 * says is its place's, by kind (12 sheet kinds, 15 confirm kinds), handed in
 * by `app`.
 */

/** The transparent cover behind an open menu: a click anywhere else closes it. */
export function MenuScrim({ onClose }: { readonly onClose: () => void }) {
  return (
    <button
      type="button"
      aria-label="Close menu"
      onClick={onClose}
      style={{ position: 'absolute', inset: 0, zIndex: 20, padding: 0, border: 0, background: 'transparent', cursor: 'default' }}
    />
  )
}

/** One line at the bottom of the window. */
export function ToastHost({ text }: { readonly text: string }) {
  return (
    <div
      role="status"
      style={{ position: 'absolute', left: '50%', bottom: 22, transform: 'translateX(-50%)', zIndex: 80, maxWidth: 'calc(100% - 48px)', padding: '9px 16px', borderRadius: 'var(--radius-card)', background: 'var(--text-primary)', color: 'var(--surface-window)', fontSize: 'var(--t-small)', boxShadow: 'var(--shadow-toast)' }}
    >
      {text}
    </div>
  )
}

/** A button of a sheet's or a confirm's row. */
export interface HostButton {
  readonly label: string
  readonly run: () => void
  /** `btn`, `btn-p` or `btn-g`: the old prototype's three. */
  readonly kind?: 'btn' | 'btn-p' | 'btn-g'
  readonly disabled?: boolean
  /** A destructive step, drawn in the error color. */
  readonly destructive?: boolean
}

function Buttons({ buttons }: { readonly buttons: readonly HostButton[] }) {
  return buttons.map((button) => (
    <button
      key={button.label}
      type="button"
      className={button.kind ?? 'btn-g'}
      onClick={button.run}
      disabled={button.disabled}
      style={button.destructive ? { background: 'var(--status-error)', color: 'var(--surface-window)' } : undefined}
    >
      {button.label}
    </button>
  ))
}

export interface SheetHostProps {
  readonly title: string
  readonly subtitle?: string
  /** Its width in px: each kind's own, from tokens.json's dialog sizes where it has one. */
  readonly width: number
  /** What the sheet says and asks: its place's. */
  readonly children: ReactNode
  readonly left?: readonly HostButton[]
  readonly right: readonly HostButton[]
}

/** The sheet host: a dialog over the scrim, its title, its place's content and its buttons. */
export function SheetHost({ title, subtitle, width, children, left = [], right }: SheetHostProps) {
  return (
    <div style={{ position: 'absolute', inset: 0, zIndex: 66, display: 'grid', placeItems: 'center', padding: 24, background: 'var(--scrim-modal)' }}>
      <div
        role="dialog"
        aria-modal="true"
        aria-labelledby="gs-h"
        style={{ width: `min(${width}px, 100%)`, maxHeight: '100%', overflowY: 'auto', border: '1px solid var(--line-strong)', borderRadius: 'var(--radius-dialog)', background: 'var(--surface-card)', boxShadow: 'var(--shadow-dialog)' }}
      >
        <div style={{ padding: '18px 20px 6px' }}>
          <h2 id="gs-h" style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600, overflowWrap: 'anywhere' }}>
            {title}
          </h2>
          {subtitle ? <p style={{ margin: '6px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{subtitle}</p> : null}
        </div>
        <div style={{ padding: '10px 20px', display: 'flex', flexDirection: 'column', gap: 12, fontSize: 'var(--t-small)' }}>{children}</div>
        <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 8, padding: '12px 20px 18px' }}>
          <Buttons buttons={left} />
          <span style={{ flex: '1 1 auto' }} />
          <Buttons buttons={right} />
        </div>
      </div>
    </div>
  )
}

export interface ConfirmHostProps {
  readonly title: string
  readonly body: string
  /** The CLI's own plan, when the step has one: what it prints without `--yes`. */
  readonly detail?: string
  readonly cancel?: HostButton
  /** A second, destructive way on: "…and the backends that use them". */
  readonly alternative?: HostButton
  readonly confirm: HostButton
}

/** The confirm host: an alert over the scrim, asking before a step that cannot be undone. */
export function ConfirmHost({ title, body, detail, cancel, alternative, confirm }: ConfirmHostProps) {
  return (
    <div style={{ position: 'absolute', inset: 0, zIndex: 70, display: 'grid', placeItems: 'center', padding: 24, background: 'var(--scrim-modal)' }}>
      <div
        role="alertdialog"
        aria-modal="true"
        aria-labelledby="cf-h"
        style={{ width: 'min(var(--layout-dialog-confirm), 100%)', border: '1px solid var(--line-strong)', borderRadius: 'var(--radius-dialog)', background: 'var(--surface-card)', boxShadow: 'var(--shadow-dialog)' }}
      >
        <div style={{ padding: '18px 20px 4px' }}>
          <h2 id="cf-h" style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>
            {title}
          </h2>
          <p style={{ margin: '6px 0 0', fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }}>{body}</p>
          {detail ? (
            <div
              className="mono"
              style={{ marginTop: 10, padding: '8px 10px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}
            >
              {detail}
            </div>
          ) : null}
        </div>
        <div style={{ display: 'flex', flexWrap: 'wrap', justifyContent: 'flex-end', gap: 8, padding: '16px 20px 18px' }}>
          <Buttons buttons={[...(cancel ? [{ kind: 'btn-g' as const, ...cancel }] : []), ...(alternative ? [{ kind: 'btn-p' as const, destructive: true, ...alternative }] : []), { kind: 'btn-p' as const, ...confirm }]} />
        </div>
      </div>
    </div>
  )
}
