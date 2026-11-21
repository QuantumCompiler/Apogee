import { Segments } from '../../styles/Components/Segments'
import { useObserved } from '../../styles/Observation/useObserved'
import type { CodeFont, ColorName, TextSize, ThemeId, UiFont } from '../../styles/Tokens/Tokens'
import { SectionHeading } from '../Components/SettingRow'
import type { ThemeSettings } from '../ViewModels/ThemeSettings'

const label = { display: 'flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' } as const
const quiet = { fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' } as const

/**
 * Appearance (design 1i): the theme's four cards and Automatic's two picks,
 * both fonts and the text size, then every element color of the theme
 * drawn, each with its color, its hex, its contrast against what it sits on
 * and its reset -- the old prototype's pane, each change through the theme
 * service, so it restyles the whole window as it is made.
 */
export function AppearancePane({ model }: { readonly model: ThemeSettings }) {
  useObserved(model)
  const face = model.face()
  return (
    <>
      <SectionHeading margin="16px 0 10px" ruled={false}>
        THEME
      </SectionHeading>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(160px, 1fr))', gap: 10 }}>
        {face.cards.map((card) => (
          <button
            key={card.id}
            type="button"
            aria-pressed={card.current}
            onClick={() => model.pick(card.id)}
            style={{ display: 'flex', flexDirection: 'column', gap: 8, padding: 10, textAlign: 'left', background: 'var(--surface-card)', border: `1px solid ${card.current ? 'var(--status-accent)' : 'var(--line-border)'}`, boxShadow: `0 0 0 1px ${card.current ? 'var(--status-accent)' : 'transparent'}`, borderRadius: 'var(--radius-card)', color: 'var(--text-primary)' }}
          >
            <span aria-hidden="true" style={{ display: 'flex', width: '100%', height: 28, borderRadius: 'var(--radius-row)', overflow: 'hidden', border: '1px solid var(--line-border)' }}>
              {card.swatches.map((swatch, index) => (
                <span key={index} style={{ flex: index === 0 ? '2 1 0' : '1 1 0', background: swatch }} />
              ))}
            </span>
            <span style={{ fontSize: 'var(--t-dense)', fontWeight: 500 }}>{card.name}</span>
            <span style={{ fontSize: 'var(--t-small)', color: card.noteColor }}>{card.note}</span>
          </button>
        ))}
      </div>
      {face.auto.shown ? (
        <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: '12px 20px', marginTop: 12, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
          <span>{face.auto.line}</span>
          <label style={label}>
            Light uses
            <select className="inp" value={face.auto.light} onChange={(event) => model.setAuto('light', event.target.value as ThemeId)}>
              {face.auto.lightOptions.map((option) => (
                <option key={option.value} value={option.value}>
                  {option.label}
                </option>
              ))}
            </select>
          </label>
          <label style={label}>
            Dark uses
            <select className="inp" value={face.auto.dark} onChange={(event) => model.setAuto('dark', event.target.value as ThemeId)}>
              {face.auto.darkOptions.map((option) => (
                <option key={option.value} value={option.value}>
                  {option.label}
                </option>
              ))}
            </select>
          </label>
        </div>
      ) : null}
      <SectionHeading margin="24px 0 10px" ruled={false}>
        TEXT
      </SectionHeading>
      <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: '12px 20px' }}>
        <label style={label}>
          Interface
          <select className="inp" value={face.fonts.ui} onChange={(event) => model.setUiFont(event.target.value as UiFont)}>
            {face.fonts.uiOptions.map((option) => (
              <option key={option.value} value={option.value}>
                {option.label}
              </option>
            ))}
          </select>
        </label>
        <label style={label}>
          Code
          <select className="inp" value={face.fonts.code} onChange={(event) => model.setCodeFont(event.target.value as CodeFont)}>
            {face.fonts.codeOptions.map((option) => (
              <option key={option.value} value={option.value}>
                {option.label}
              </option>
            ))}
          </select>
        </label>
        <Segments label="Text size" options={face.sizes.map((size) => [size.value, size.label] as const)} current={face.sizes.find((size) => size.current)?.value ?? ''} pick={(value) => model.setTextSize(value as TextSize)} inline={false} />
      </div>
      <p style={{ margin: '8px 0 0', ...quiet }}>{face.sizesLine}</p>
      <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: '6px 12px', marginTop: 26 }}>
        <SectionHeading margin="0" ruled={false}>
          {face.colors.title}
        </SectionHeading>
        <span style={quiet}>{face.colors.changes}</span>
        <span style={{ fontSize: 'var(--t-small)', color: face.colors.lowColor }}>{face.colors.low}</span>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="btn-g" onClick={() => model.resetAll()} disabled={face.colors.noChanges} style={{ height: 26, fontSize: 'var(--t-small)' }}>
          Reset all changes
        </button>
      </div>
      <p style={{ margin: '6px 0 0', ...quiet }}>Every color in Apogee. Changes apply to all windows as you make them; contrast is measured against what each element sits on.</p>
      {face.colors.groups.map((group) => (
        <div key={group.title}>
          <h4 className="lbl" style={{ margin: '18px 0 0', paddingBottom: 6, borderBottom: '1px solid var(--line-border)' }}>
            {group.title}
          </h4>
          {group.rows.map((row) => (
            <div key={row.key} style={{ display: 'grid', gridTemplateColumns: '32px minmax(0, 1fr) 88px 110px 28px', gap: 12, alignItems: 'center', minHeight: 44, padding: '4px 0', borderBottom: '1px solid var(--line-border)' }}>
              <input
                type="color"
                value={row.value.toLowerCase()}
                onChange={(event) => model.pickColor(row.key as ColorName, event.target.value)}
                aria-label={`${row.label} color`}
                style={{ width: 32, height: 26, padding: 0, border: '1px solid var(--line-strong)', borderRadius: 'var(--radius-control)', background: 'transparent', cursor: 'pointer' }}
              />
              <div style={{ minWidth: 0, lineHeight: 1.35 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-body)' }}>
                  {row.label}
                  {row.edited ? <span title="Changed from the theme" style={{ width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: 'var(--status-accent)' }} /> : null}
                </div>
                <div style={{ ...quiet, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                  <span className="mono" style={{ fontSize: 'var(--t-label)' }}>
                    {row.key}
                  </span>{' '}
                  · {row.where}
                </div>
              </div>
              <input
                type="text"
                className="mono inp"
                value={row.hexShown}
                onChange={(event) => model.typeHex(row.key as ColorName, event.target.value)}
                onBlur={() => model.leaveHex(row.key as ColorName)}
                aria-label={`${row.label} hex value`}
                style={{ width: '100%', height: 26, fontSize: 'var(--t-ui)' }}
              />
              <span className="mono" title={row.badgeTitle} style={{ display: 'inline-flex', gap: 6, fontSize: 'var(--t-meta)', color: row.badgeColor }}>
                {row.ratioText}
                <span>{row.badgeNote}</span>
              </span>
              {row.edited ? (
                <button type="button" className="ib" onClick={() => model.reset(row.key as ColorName)} aria-label={`Reset ${row.label} to ${row.baseHex}`} title={`Reset to ${row.baseHex}`} style={{ width: 28, height: 26 }}>
                  <svg className="i" width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
                    <path d="M3 8a5 5 0 1 0 1.5-3.5" />
                    <path d="M3 2.5V5h2.5" />
                  </svg>
                </button>
              ) : (
                <span />
              )}
            </div>
          ))}
        </div>
      ))}
      <p style={{ margin: '16px 0 0', ...quiet }}>
        Saved by the app to <span className="mono">~/Library/Application Support/Apogee/themes/{face.colors.file}</span>. Only your changes are written, and the command line never reads it.
      </p>
    </>
  )
}
