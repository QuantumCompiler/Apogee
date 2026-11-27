/**
 * A segmented choice, the old prototype's `seg` (design 1h's, promoted to
 * `styles` by 1i on its second use): a group of buttons, the one chosen
 * pressed and drawn on the control's surface.
 */
export function Segments({ label, options, current, pick, inline = true }: { readonly label: string; readonly options: ReadonlyArray<readonly [string, string]>; readonly current: string; readonly pick: (value: string) => void; readonly inline?: boolean }) {
  return (
    <div role="group" aria-label={label} className="seg" style={inline ? { display: 'inline-flex' } : undefined}>
      {options.map(([value, words]) => (
        <button key={value} type="button" aria-pressed={current === value} onClick={() => pick(value)} style={{ background: current === value ? 'var(--surface-control)' : 'transparent', color: current === value ? 'var(--text-primary)' : 'var(--text-secondary)', fontWeight: current === value ? 600 : 400 }}>
          {words}
        </button>
      ))}
    </div>
  )
}
