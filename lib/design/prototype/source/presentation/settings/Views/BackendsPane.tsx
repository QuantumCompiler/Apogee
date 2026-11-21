import { useObserved } from '../../styles/Observation/useObserved'
import { SectionHeading } from '../Components/SettingRow'
import type { BackendRowFace, KeyRowFace, SettingsViewModel } from '../ViewModels/SettingsViewModel'
import { Icon } from '../../styles/Components/Icon'

const small = { height: 26, fontSize: 'var(--t-small)' } as const

/** A row's glyph by where it runs: a chip on this Mac, a stack for an API, the loop of a subscription -- the old pane's three. */
function Glyph({ kind }: { readonly kind: BackendRowFace['kind'] }) {
  if (kind === 'api') {
    return <Icon name="api" size={14} style={{ color: 'var(--model-api)' }} />
  }
  if (kind === 'subscription') {
    return <Icon name="subscription" size={14} style={{ color: 'var(--model-subscription)' }} />
  }
  return <Icon name="local" size={14} style={{ color: 'var(--model-local)' }} />
}

/** One row: its glyph, its name and status, its detail and the lines under it, its buttons, and -- a key row being edited -- the key's field. */
function BackendRow({ model, row, draft = '', empty = true }: { readonly model: SettingsViewModel; readonly row: BackendRowFace | KeyRowFace; readonly draft?: string; readonly empty?: boolean }) {
  const key = 'provider' in row ? row : undefined
  return (
    <div style={{ display: 'grid', gridTemplateColumns: '18px minmax(0, 1fr) auto', gap: '8px 12px', alignItems: 'center', padding: '12px 0', borderBottom: '1px solid var(--line-border)' }}>
      <span style={{ display: 'grid', placeItems: 'center' }}>
        <Glyph kind={row.kind} />
      </span>
      <div style={{ minWidth: 0 }}>
        <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8 }}>
          <span style={{ fontWeight: 500 }}>{row.name}</span>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)', color: row.statusColor }}>
            <span style={{ width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: row.statusColor }} />
            {row.status}
          </span>
        </div>
        {row.detail !== '' ? <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{row.detail}</div> : null}
        {row.consent.map((line) => (
          <div key={line} style={{ marginTop: 2, fontSize: 'var(--t-meta)', color: row.consentColor, overflowWrap: 'anywhere' }}>
            {line}
          </div>
        ))}
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, justifyContent: 'flex-end' }}>
        {row.actions.map((action) => (
          <button key={action.label} type="button" className={action.kind} onClick={() => {
              const act = action.act
              if (act.case === 'installRuntime') model.installRuntime(act.row)
              else if (act.case === 'editKey') model.editKey(act.provider)
              else void model.removeKey(act.provider)
            }} style={small}>
            {action.label}
          </button>
        ))}
      </div>
      {key?.editing ? (
        <div style={{ gridColumn: '2 / -1', display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 8 }}>
          <input type="password" className="inp mono" value={draft} onChange={(event) => model.typeKey(event.target.value)} placeholder={key.placeholder} aria-label={`${key.name} key`} autoComplete="off" style={{ flex: '1 1 260px', minWidth: 0 }} />
          <button type="button" className="btn-p" onClick={() => void model.saveKey()} disabled={empty} style={{ height: 28 }}>
            Save key
          </button>
          <button type="button" className="btn-g" onClick={() => model.cancelKey()} style={{ height: 28 }}>
            Cancel
          </button>
          <span style={{ flex: '1 1 100%', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Prototype: nothing you type here is stored or sent.</span>
        </div>
      ) : null}
    </div>
  )
}

/**
 * Backends & keys (design 1i): the old pane's sections over the CLI -- the
 * backends on this Mac (`models list`), the Python environment's runtimes
 * (`check`'s Video rows), the API keys (`auth list`, each provider's
 * backends and their vendor's tools), the subscriptions -- then the
 * providers the scan found, with Register…, and Health (`check`), with its
 * fix as a job and Stop.
 */
export function BackendsPane({ model }: { readonly model: SettingsViewModel }) {
  useObserved(model)
  const all = model.face()
  const face = all.backends
  const health = face.health
  return (
    <>
      <p style={{ margin: '16px 0 6px', fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }}>Where models can run. A saved key is never shown again, only whether it’s set. Keys are stored beside the config, readable only by you.</p>
      {face.sections.map((section) => (
        <div key={section.title}>
          <SectionHeading margin="20px 0 0">{section.title}</SectionHeading>
          {section.rows.map((row) => (
            <BackendRow key={row.name} model={model} row={row} />
          ))}
        </div>
      ))}
      <SectionHeading margin="20px 0 0">CLOUD APIS · BILLED PER USE</SectionHeading>
      {face.keys.map((row) => (
        <BackendRow key={row.provider} model={model} row={row} draft={face.keyDraft} empty={face.keyEmpty} />
      ))}
      {face.subscriptions.map((section) => (
        <div key={section.title}>
          <SectionHeading margin="20px 0 0">{section.title}</SectionHeading>
          {section.rows.map((row) => (
            <BackendRow key={row.name} model={model} row={row} />
          ))}
        </div>
      ))}
      <SectionHeading margin="24px 0 0">PROVIDERS ON THIS MACHINE</SectionHeading>
      {face.providers.map((provider) => (
        <div key={provider.id} style={{ display: 'flex', alignItems: 'center', gap: 12, minHeight: 40, padding: '4px 0', borderBottom: '1px solid var(--line-border)' }}>
          <span style={{ flex: '1 1 auto', minWidth: 0 }}>{provider.label}</span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>
            {provider.state}
          </span>
          {provider.canRegister ? (
            <button type="button" className="btn-g" onClick={() => model.askToRegister(provider.id)} disabled={all.working !== ''} style={small}>
              Register…
            </button>
          ) : null}
        </div>
      ))}
      {face.registered.shown ? (
        <div style={{ marginTop: 12, padding: '10px 12px', border: '1px solid var(--line-border)', borderRadius: 'var(--radius-control)' }}>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 8 }}>
            <span style={{ flex: '1 1 auto', fontSize: 'var(--t-small)', fontWeight: 600 }}>{face.registered.title}</span>
            <button type="button" className="btn-g" onClick={() => model.dismissRegistered()} style={{ height: 24, fontSize: 'var(--t-small)' }}>
              Dismiss
            </button>
          </div>
          <div className="mono" style={{ marginTop: 6, fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
            {face.registered.text}
          </div>
        </div>
      ) : null}
      <SectionHeading margin="24px 0 0">HEALTH</SectionHeading>
      <p style={{ margin: '10px 0 6px', fontSize: 'var(--t-body)', color: health.summaryColor }}>{health.summary}</p>
      {health.rows.map((row) => (
        <div key={row.text} style={{ padding: '6px 0', borderBottom: '1px solid var(--line-border)' }}>
          <div style={{ fontSize: 'var(--t-small)', color: row.color, overflowWrap: 'anywhere' }}>{row.text}</div>
          {row.remedy !== '' ? (
            <div className="mono" style={{ marginTop: 2, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
              {row.remedy}
            </div>
          ) : null}
        </div>
      ))}
      <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 8, marginTop: 12 }}>
        <button type="button" className="btn-g" onClick={() => model.fix(['check', '--fix'])} disabled={health.fixing} style={small}>
          Fix what can be fixed (check --fix)
        </button>
        <button type="button" className="btn-g" onClick={() => model.fix(['bootstrap'])} disabled={health.fixing} style={small}>
          Install runtimes and register providers (bootstrap)
        </button>
        {health.fixing ? (
          <>
            <span className="mono" style={{ display: 'inline-flex', alignItems: 'center', gap: 8, minWidth: 0, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', whiteSpace: 'pre', overflow: 'hidden', textOverflow: 'ellipsis' }}>
              <span className="pulse" style={{ flex: '0 0 auto', width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: 'var(--status-accent)' }} />
              {health.busy}
            </span>
            <button type="button" className="btn-g" onClick={() => void model.stopFix()} disabled={!health.canStop} aria-label={`Stop ${health.command}`} style={{ height: 24, fontSize: 'var(--t-small)' }}>
              Stop
            </button>
          </>
        ) : null}
      </div>
    </>
  )
}

/** Register…'s sheet content (the old `registerProvider`, the Mac's `RegisterProviderSheet`): Ollama's model where its type needs one, and the command as typed. The shell's sheet host frames it. */
export function RegisterProviderSheet({ model }: { readonly model: SettingsViewModel }) {
  useObserved(model)
  const sheet = model.face().register
  return (
    <>
      {sheet.needsModel ? <input type="text" className="inp" value={sheet.model} onChange={(event) => model.typeRegisterModel(event.target.value)} placeholder="The model it runs, or none for apogee to choose" aria-label="The model it runs" style={{ height: 32 }} /> : null}
      <div className="mono" style={{ padding: '8px 10px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
        {sheet.command}
      </div>
    </>
  )
}
