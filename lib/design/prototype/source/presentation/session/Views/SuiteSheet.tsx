import { useObserved } from '../../styles/Observation/useObserved'
import type { SessionViewModel } from '../ViewModels/SessionViewModel'

/**
 * The suite a session runs under, chosen (`main.html:7790-7814` at
 * `b375fac9`, the generic sheet's kind `suite`): each configured suite with
 * the CLI's own verdict on its fit here, or none; one the CLI prices over
 * this machine's memory runs only forced, its total in the CLI's words. The
 * Mac app's `SuiteSheet`. The sheet's frame, title and buttons are the
 * shell's host's, handed this content by `app`.
 */
export function SuiteSheet({ model }: { readonly model: SessionViewModel }) {
  useObserved(model)
  const sheet = model.face().suiteSheet
  return (
    <>
      {sheet.rows.length === 0 ? (
        <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>No suites are configured. apogee config add-suite &lt;name&gt; --chat &lt;backend&gt; … makes one.</p>
      ) : null}
      <div role="radiogroup" aria-label="Suite" style={{ display: 'flex', flexDirection: 'column', border: '1px solid var(--line-border)', borderRadius: 8, overflow: 'hidden' }}>
        {sheet.rows.map((row) => (
          <button
            key={row.name}
            type="button"
            role="radio"
            aria-checked={row.chosen}
            onClick={() => model.chooseRow(row.name)}
            className="hv"
            style={{
              display: 'grid',
              gridTemplateColumns: '14px minmax(0, 1fr) auto',
              columnGap: 10,
              alignItems: 'center',
              minHeight: 40,
              padding: '6px 12px',
              border: 0,
              borderBottom: '1px solid var(--line-border)',
              background: row.chosen ? 'var(--surface-selection)' : undefined,
              color: 'var(--text-primary)',
              textAlign: 'left',
            }}
          >
            <span
              aria-hidden="true"
              style={{
                width: 12,
                height: 12,
                boxSizing: 'border-box',
                borderRadius: '50%',
                border: row.chosen ? '4px solid var(--status-accent)' : '1.5px solid var(--line-strong)',
                background: row.chosen ? 'var(--surface-card)' : undefined,
              }}
            />
            <span style={{ minWidth: 0, fontSize: 'var(--t-body)', overflowWrap: 'anywhere' }}>
              {row.title}
              {row.running ? (
                <span className="mono" style={{ marginLeft: 8, fontSize: 'var(--t-label)', color: 'var(--status-accent)' }}>
                  now
                </span>
              ) : null}
            </span>
            <span className="mono" style={{ maxWidth: 260, fontSize: 'var(--t-meta)', color: row.over ? 'var(--status-error)' : 'var(--text-tertiary)', textAlign: 'right' }}>
              {row.verdict}
            </span>
          </button>
        ))}
      </div>
      {sheet.overBudget ? (
        <label style={{ display: 'flex', alignItems: 'flex-start', gap: 8, color: 'var(--text-primary)', cursor: 'pointer' }}>
          <input type="checkbox" checked={sheet.force} onChange={() => model.toggleForce()} style={{ margin: '2px 0 0', accentColor: 'var(--status-accent)' }} />
          <span>Run it anyway (--force): apogee prices it over this machine&apos;s memory</span>
        </label>
      ) : null}
      {sheet.total !== '' ? (
        <p className="mono" style={{ margin: 0, color: 'var(--status-error)', fontSize: 'var(--t-meta)', overflowWrap: 'anywhere', whiteSpace: 'pre-wrap' }}>
          {sheet.total}
        </p>
      ) : null}
      <p style={{ margin: 0, color: 'var(--text-tertiary)', overflowWrap: 'anywhere', whiteSpace: 'pre-wrap' }}>
        Machine mode carries no suite: the session restarts under it -- the conversation carried on with --resume, on the suite&apos;s chat member -- and a local model loads again.
      </p>
    </>
  )
}
