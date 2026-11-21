import type { ReactNode } from 'react'

/** A setting's row, the old panes' two columns: its name, 200 px, and what sets it, a rule under all but the last. */
export function SettingRow({ label, children, last = false }: { readonly label: string; readonly children: ReactNode; readonly last?: boolean }) {
  return (
    <div style={{ display: 'grid', gridTemplateColumns: '200px minmax(0, 1fr)', gap: 16, padding: '16px 0', borderBottom: last ? undefined : '1px solid var(--line-border)' }}>
      <div style={{ fontWeight: 500 }}>{label}</div>
      <div style={{ minWidth: 0 }}>{children}</div>
    </div>
  )
}

/** A section's heading, the old panes' `lbl` in capitals over a rule. */
export function SectionHeading({ children, margin = '18px 0 0', ruled = true }: { readonly children: ReactNode; readonly margin?: string; readonly ruled?: boolean }) {
  return (
    <h3 className="lbl" style={{ margin, ...(ruled ? { paddingBottom: 6, borderBottom: '1px solid var(--line-border)' } : {}) }}>
      {children}
    </h3>
  )
}

/** A note under a setting: small, tertiary, the old panes' `t-small`. */
export function Note({ children, color = 'var(--text-tertiary)', margin = '6px 0 0' }: { readonly children: ReactNode; readonly color?: string; readonly margin?: string }) {
  return <div style={{ margin, fontSize: 'var(--t-small)', color }}>{children}</div>
}
