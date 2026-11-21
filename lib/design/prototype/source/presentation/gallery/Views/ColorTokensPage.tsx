import type { CSSProperties } from 'react'
import type { ColorPageFace } from '../ViewModels/ColorPage'
import { FoundationsHeader, foundationsPage } from '../Components/FoundationsHeader'

const grid: CSSProperties = { display: 'grid', gridTemplateColumns: '200px minmax(0, 1fr) repeat(3, 150px)', columnGap: 16, alignItems: 'center', minHeight: 46, borderBottom: '1px solid var(--line-border)' }

/** A ratio's badge, as Settings › Appearance draws it: low in the warning color, too low in the error color. */
const noteColor = { '': 'var(--text-tertiary)', low: 'var(--status-warning)', 'too low': 'var(--status-error)' } as const

/**
 * The Foundations color page (design 1q): the old ColorTokens board,
 * generated -- the 25 tokens in tokens.json's groups, each with its label,
 * what it is used for and what it is measured against, and its value and
 * ratio in each theme, the ratio computed by `styles`' `ratio()` as
 * Settings › Appearance's badges compute it. The board's theme is the
 * gallery's frame's; the swatches show every theme's value whichever it is.
 */
export function ColorTokensPage({ face }: { readonly face: ColorPageFace }) {
  const dark = face.themes.find((theme) => theme.id === 'dark')
  return (
    <div style={foundationsPage}>
      <FoundationsHeader label="FOUNDATIONS · COLOR" title="Color tokens">
        Every color in Apogee is one of these {face.count} tokens. A theme sets all of them, and in Settings › Appearance a user can change any one of them for any theme. The names are the same in the SwiftUI and Qt apps and in theme files.
      </FoundationsHeader>
      <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'flex-start', gap: 40 }}>
        <div role="table" aria-label="Color tokens" style={{ flex: '999 1 860px', minWidth: 0, overflowX: 'auto' }}>
          <div role="row" className="mono" style={{ ...grid, minHeight: 34, fontSize: 'var(--t-label)', letterSpacing: '.08em', color: 'var(--text-tertiary)', borderBottom: '1px solid var(--line-strong)' }}>
            <span role="columnheader">TOKEN</span>
            <span role="columnheader">USED FOR · MEASURED AGAINST</span>
            {face.themes.map((theme) => (
              <span role="columnheader" key={theme.id}>
                {theme.name.toUpperCase()}
              </span>
            ))}
          </div>
          {face.groups.map((group) => (
            <div role="rowgroup" key={group.title} aria-label={group.title}>
              <div role="row" className="mono" style={{ padding: '22px 0 8px', fontSize: 'var(--t-label)', letterSpacing: '.08em', color: 'var(--text-secondary)' }}>
                <span role="rowheader">{group.title}</span>
              </div>
              {group.rows.map((row) => (
                <div role="row" key={row.name} style={grid}>
                  <span role="rowheader" style={{ display: 'flex', flexDirection: 'column', padding: '6px 0' }}>
                    <span className="mono" style={{ fontSize: 'var(--t-small)' }}>
                      {row.name}
                    </span>
                    <span style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{row.label}</span>
                  </span>
                  <span role="cell" style={{ display: 'flex', flexDirection: 'column', padding: '6px 0' }}>
                    <span style={{ color: 'var(--text-secondary)' }}>{row.usedFor}</span>
                    <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                      against {row.against}
                    </span>
                  </span>
                  {row.cells.map((cell) => (
                    <span role="cell" key={cell.theme} style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                      <span aria-hidden="true" style={{ flex: '0 0 22px', width: 22, height: 22, boxSizing: 'border-box', borderRadius: 'var(--radius-row)', border: '1px solid var(--line-strong)', background: cell.hex }} />
                      <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
                        {cell.hex}
                        <br />
                        <span style={{ color: noteColor[cell.note] }}>
                          {cell.text}
                          {cell.note === '' ? '' : ` · ${cell.note}`}
                        </span>
                      </span>
                    </span>
                  ))}
                </div>
              ))}
            </div>
          ))}
          <p style={{ margin: '14px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>
            Each ratio is computed from tokens.json by the function Settings › Appearance’s badges use: under 4.5:1 is low, under 3:1 too low. Borders are decoration, with no minimum.
          </p>
        </div>
        <aside style={{ flex: '1 1 340px', minWidth: 0, display: 'flex', flexDirection: 'column', gap: 28 }}>
          <section>
            <h2 style={{ margin: '0 0 4px', fontSize: 'var(--t-h3)', fontWeight: 600 }}>A theme file</h2>
            <p style={{ margin: '0 0 12px', fontSize: 'var(--t-dense)', color: 'var(--text-secondary)' }}>
              A user’s changes, and nothing else. The same file works on macOS, Windows and Linux. The app owns it; the command line never reads it.
            </p>
            <div className="mono" style={{ padding: '8px 12px', border: '1px solid var(--line-border)', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
              ~/Library/Application Support/Apogee/themes/{dark?.file ?? ''}
            </div>
          </section>
          <section>
            <h2 style={{ margin: '0 0 10px', fontSize: 'var(--t-h3)', fontWeight: 600 }}>How a color is chosen</h2>
            <ol style={{ margin: 0, paddingLeft: 20, display: 'flex', flexDirection: 'column', gap: 8, color: 'var(--text-secondary)' }}>
              <li>The user’s change to this token in this theme.</li>
              <li>The theme’s own value.</li>
              <li>For a token the theme doesn’t name (a file written before the token existed), Graphite’s value. Settings lists it as “from Graphite”.</li>
            </ol>
          </section>
          <section>
            <h2 style={{ margin: '0 0 10px', fontSize: 'var(--t-h3)', fontWeight: 600 }}>Rules for built-in themes</h2>
            <ul style={{ margin: 0, paddingLeft: 20, display: 'flex', flexDirection: 'column', gap: 8, color: 'var(--text-secondary)' }}>
              <li>All text tokens meet 4.5:1 on every surface they sit on.</li>
              <li>Model types differ in hue and lightness, and always come with an icon and a word: color is never the only signal.</li>
              <li>Amber means spending: money on a cloud API, or room in the context window.</li>
            </ul>
          </section>
        </aside>
      </div>
    </div>
  )
}
