import { Segments } from '../../styles/Components/Segments'
import { useObserved } from '../../styles/Observation/useObserved'
import { SectionHeading } from '../Components/SettingRow'
import type { SettingsViewModel } from '../ViewModels/SettingsViewModel'

const small = { height: 26, fontSize: 'var(--t-small)' } as const
const entry = { display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: 12, alignItems: 'center', padding: '10px 0', borderBottom: '1px solid var(--line-border)' } as const
const quiet = { margin: '10px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' } as const

/** Permissions: each tool's gate level, as `config get permissions` says it, and what a card allowed always or for a session. */
export function PermissionsPane({ model }: { readonly model: SettingsViewModel }) {
  useObserved(model)
  const face = model.face().permissions
  return (
    <>
      <p style={{ margin: '16px 0 6px', fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }}>What a model may do without asking. Reading files never asks; writing, running commands and visiting new websites do, unless you say otherwise here or on the card.</p>
      <SectionHeading>TOOLS</SectionHeading>
      {face.tools.map((tool) => (
        <div key={tool.name} style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: '8px 16px', alignItems: 'center', padding: '12px 0', borderBottom: '1px solid var(--line-border)' }}>
          <div>
            <div className="mono" style={{ fontSize: 'var(--t-ui)' }}>
              {tool.name}
            </div>
            <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{tool.desc}</div>
          </div>
          <Segments label={`${tool.name} policy`} options={tool.levels.map((level) => [level.value, level.label] as const)} current={tool.level} pick={(level) => void model.setPermission(tool.name, level)} inline={false} />
        </div>
      ))}
      <SectionHeading margin="22px 0 0">ALWAYS ALLOWED, FROM A PERMISSION CARD</SectionHeading>
      {face.always.map((allowed, index) => (
        <div key={`${allowed.label} ${index}`} style={entry}>
          <div>
            <div className="mono" style={{ fontSize: 'var(--t-ui)' }}>
              {allowed.label}
            </div>
            <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{allowed.from}</div>
          </div>
          <button type="button" className="btn-g" onClick={() => void model.removeAlways(index)} style={small}>
            Remove
          </button>
        </div>
      ))}
      {face.noAlways ? <p style={quiet}>Nothing yet. Choosing “Always allow” on a permission card adds it here.</p> : null}
      <SectionHeading margin="22px 0 0">ALLOWED FOR A SESSION</SectionHeading>
      {face.session.map((allowed, index) => (
        <div key={`${allowed.label} ${index}`} style={entry}>
          <div>
            <div className="mono" style={{ fontSize: 'var(--t-ui)' }}>
              {allowed.label}
            </div>
            <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{allowed.from}</div>
          </div>
          <button type="button" className="btn-g" onClick={() => model.revokeSession(index)} style={small}>
            Revoke
          </button>
        </div>
      ))}
      {face.noSession ? <p style={quiet}>Nothing is allowed just for a session right now.</p> : null}
    </>
  )
}

/** Web access: the hosts fetch_url reaches without asking, as the config lists them, and the web search address. */
export function WebPane({ model }: { readonly model: SettingsViewModel }) {
  useObserved(model)
  const face = model.face().web
  return (
    <>
      <SectionHeading>ALLOWED WEBSITES</SectionHeading>
      {face.sites.map((host) => (
        <div key={host} style={{ ...entry, padding: '8px 0' }}>
          <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
            {host}
          </span>
          <button type="button" className="btn-g" onClick={() => void model.removeSite(host)} style={small}>
            Remove
          </button>
        </div>
      ))}
      <div style={{ display: 'flex', gap: 8, marginTop: 10 }}>
        <input type="text" className="inp mono" value={face.siteDraft} onChange={(event) => model.typeSite(event.target.value)} placeholder="example.com" aria-label="Website to allow" style={{ flex: '0 1 280px' }} />
        <button type="button" className="btn" onClick={() => void model.addSite()} disabled={face.siteEmpty}>
          Add
        </button>
      </div>
      <p style={{ margin: '8px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Any other website asks the first time. Redirects are followed one hop at a time, and each hop is checked against this list.</p>
      <SectionHeading margin="24px 0 0">WEB SEARCH</SectionHeading>
      <div style={{ display: 'grid', gridTemplateColumns: '200px minmax(0, 1fr)', gap: 16, padding: '14px 0' }}>
        <div style={{ fontWeight: 500 }}>Your SearXNG</div>
        <div>
          <div className="mono" title="apogee config get tools.search.url" style={{ fontSize: 'var(--t-ui)', color: face.searchShownColor, overflowWrap: 'anywhere' }}>
            {face.search}
          </div>
          <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
            <input type="text" className="inp mono" value={face.searchDraft} onChange={(event) => model.typeSearch(event.target.value)} placeholder="http://localhost:8888" aria-label="SearXNG address" style={{ flex: '0 1 360px', minWidth: 0 }} />
            <button type="button" className="btn" onClick={() => void model.setSearch()} disabled={face.searchEmpty}>
              Set
            </button>
          </div>
          <div style={{ marginTop: 6, fontSize: 'var(--t-small)', color: face.searchColor }}>{face.searchNote}</div>
        </div>
      </div>
    </>
  )
}
