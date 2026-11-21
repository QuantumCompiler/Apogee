import { ModelGlyph } from '../../styles/Components/ModelGlyph'
import { useObserved } from '../../styles/Observation/useObserved'
import type { BackendRow, ModelsViewModel } from '../ViewModels/ModelsViewModel'
import { DeleteNarrationCard } from './DeleteNarrationCard'
import { DownloadsView } from './DownloadsView'
import { StoreView } from './StoreView'
import { Icon } from '../../styles/Components/Icon'

/**
 * The Models place (design 1h): the old prototype's screen (`main.html`
 * at `b375fac9`, `ui.isModels`, 860-1083) -- its header with the four tabs,
 * the store's line and Download from Hugging Face…, then Backends, Stored
 * weights, Jobs or Roles -- drawn from the place's view-model.
 */
export function ModelsScreen({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 16, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Models</h1>
        <div role="tablist" aria-label="Show" className="seg">
          {face.tabs.map((tab) => (
            <button
              key={tab.tab}
              type="button"
              role="tab"
              aria-selected={tab.current}
              onClick={() => model.pick(tab.tab)}
              style={{ display: 'inline-flex', alignItems: 'center', gap: 6, background: tab.current ? 'var(--surface-control)' : 'transparent', color: tab.current ? 'var(--text-primary)' : 'var(--text-secondary)', fontWeight: tab.current ? 600 : 400 }}
            >
              {tab.label}
              {tab.count !== '' ? (
                <span className="mono" style={{ fontSize: 'var(--t-label)', color: tab.running ? 'var(--status-accent)' : 'var(--text-tertiary)' }}>
                  {tab.count}
                </span>
              ) : null}
            </button>
          ))}
        </div>
        <span style={{ flex: '1 1 auto' }} />
        <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap' }}>
          {face.storeLine}
        </span>
        <button type="button" className="btn-p" onClick={() => model.openDownload()}>
          <Icon name="download" size={13} />
          Download from Hugging Face…
        </button>
      </header>
      {face.tab === 'backends' ? <BackendsTab model={model} /> : null}
      {face.tab === 'store' ? <StoreView model={model.store} /> : null}
      {face.tab === 'jobs' ? <DownloadsView model={model.downloads} /> : null}
      {face.tab === 'roles' ? <RolesTab model={model} /> : null}
    </div>
  )
}

const columns = 'minmax(180px, 1fr) 118px 64px 58px 70px 124px'
const glyphLabels = { local: 'local', api: 'cloud API, billed', subscription: 'subscription', none: '' } as const

function Row({ row, onSelect }: { readonly row: BackendRow; readonly onSelect: () => void }) {
  return (
    <button
      type="button"
      onClick={onSelect}
      aria-current={row.current}
      className="hv"
      style={{ width: '100%', display: 'grid', gridTemplateColumns: columns, columnGap: 12, alignItems: 'center', minHeight: 44, padding: '4px 8px', margin: '0 -8px', boxSizing: 'content-box', border: 0, borderBottom: '1px solid var(--line-border)', borderRadius: 0, background: row.current ? 'var(--surface-selection)' : '', color: 'var(--text-primary)', textAlign: 'left', fontSize: 'var(--t-body)' }}
    >
      <span style={{ display: 'flex', alignItems: 'center', gap: 10, minWidth: 0 }}>
        {row.kind === 'none' ? <span aria-hidden="true" style={{ flex: '0 0 14px' }} /> : <ModelGlyph kind={row.kind} label={glyphLabels[row.kind]} />}
        <span style={{ minWidth: 0, display: 'flex', flexDirection: 'column', lineHeight: 1.3 }}>
          <span style={{ display: 'flex', alignItems: 'center', gap: 6, minWidth: 0 }}>
            <span style={{ whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{row.name}</span>
            {row.draft.shown ? (
              <span role="img" aria-label={row.draft.aria} title={row.draft.tip} style={{ display: 'inline-flex', flex: '0 0 auto', color: row.draft.ok ? 'var(--text-tertiary)' : 'var(--status-warning)' }}>
                <Icon name="drafter" size={12} />
              </span>
            ) : null}
          </span>
          {row.roles !== '' ? (
            <span className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--status-accent)' }}>
              {row.roles}
            </span>
          ) : null}
        </span>
      </span>
      <span className="mono" title={[row.format, row.formatSub].filter((part) => part !== '').join(' · ')} style={{ minWidth: 0, display: 'flex', flexDirection: 'column', lineHeight: 1.3, fontSize: 'var(--t-ui)', color: 'var(--text-secondary)' }}>
        <span style={{ color: row.formatColor, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{row.format}</span>
        {row.formatSub !== '' ? <span style={{ fontSize: 'var(--t-meta)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{row.formatSub}</span> : null}
      </span>
      <span className="mono" style={{ fontSize: 'var(--t-ui)', textAlign: 'right', color: 'var(--text-secondary)' }}>
        {row.size}
      </span>
      <span className="mono" style={{ fontSize: 'var(--t-ui)', textAlign: 'right', color: 'var(--text-secondary)' }}>
        {row.window}
      </span>
      <span className="mono" style={{ fontSize: 'var(--t-ui)', textAlign: 'right', color: 'var(--text-secondary)' }}>
        {row.memory}
      </span>
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)', color: row.statusColor }}>
        <span style={{ width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: row.dot }} />
        {row.status}
      </span>
    </button>
  )
}

const close = (
  <Icon name="close" size={13} />
)

/** Backends: the configured backends under the prototype's two sections, and one's details beside them. */
export function BackendsTab({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  const face = model.face().backends
  const detail = face.detail
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <div style={{ flex: '1 1 0', minWidth: 0, overflow: 'auto', padding: '8px 24px 24px' }}>
        {face.narration.shown ? <DeleteNarrationCard title={face.narration.title} text={face.narration.text} dismiss={() => model.dismissNarration()} /> : null}
        <div className="lbl" style={{ display: 'grid', gridTemplateColumns: columns, columnGap: 12, alignItems: 'center', height: 34, borderBottom: '1px solid var(--line-strong)' }}>
          <span>NAME</span>
          <span>FORMAT</span>
          <span style={{ textAlign: 'right' }}>ON DISK</span>
          <span style={{ textAlign: 'right' }}>WINDOW</span>
          <span style={{ textAlign: 'right' }}>MEMORY</span>
          <span>STATUS</span>
        </div>
        {face.sections.map((section) => (
          <div key={section.title}>
            <div className="lbl" style={{ padding: '16px 0 6px' }}>
              {section.title}
            </div>
            {section.rows.map((row) => (
              <Row key={row.name} row={row} onSelect={() => void model.toggle(row.name)} />
            ))}
          </div>
        ))}
        {face.empty !== '' ? <p style={{ margin: '24px 0', color: 'var(--text-tertiary)' }}>{face.empty}</p> : null}
        <p style={{ margin: '18px 0 0', maxWidth: 760, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>
          A backend is a model sessions, suites and roles can use: weights on this Mac registered from the store, or a cloud service. Memory is the weights plus the attention cache at the model’s default window here, as its files state them; cloud models use none. Weights that aren’t a backend yet are under Stored weights.
        </p>
      </div>
      {detail.shown ? (
        <aside className="ap-detail" aria-label="Model details" style={{ flex: '0 0 380px', minHeight: 0, overflowY: 'auto', background: 'var(--surface-inspector)', borderLeft: '1px solid var(--line-border)', padding: '18px 20px 24px', fontSize: 'var(--t-small)' }}>
          <div style={{ display: 'flex', alignItems: 'flex-start', gap: 8 }}>
            <div style={{ flex: '1 1 auto', minWidth: 0 }}>
              <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', lineHeight: 1.3, fontWeight: 600, overflowWrap: 'anywhere' }}>{detail.name}</h2>
              <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8, marginTop: 6 }}>
                <span className="mono" style={{ padding: '1px 6px', borderRadius: 4, background: 'var(--surface-control)', color: detail.tagColor, fontSize: 'var(--t-label)' }}>
                  {detail.tag}
                </span>
                <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, color: detail.statusColor }}>
                  <span style={{ width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: detail.dot }} />
                  {detail.status}
                </span>
              </div>
            </div>
            <button type="button" className="ib" onClick={() => model.close()} aria-label="Close details">
              {close}
            </button>
          </div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, margin: '16px 0 6px' }}>
            {detail.actions.map((action) => (
              <button key={action.action} type="button" className={action.kind} onClick={() => void model.act(action.action)} disabled={action.disabled}>
                {action.label}
              </button>
            ))}
          </div>
          {detail.note !== '' ? <p style={{ margin: '8px 0 0', color: detail.noteColor }}>{detail.note}</p> : null}
          {detail.failure !== '' ? <p style={{ margin: '8px 0 0', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{`The details could not be read: ${detail.failure}`}</p> : null}
          <h3 className="lbl" style={{ margin: '20px 0 8px' }}>
            FACTS
          </h3>
          <div style={{ display: 'grid', gridTemplateColumns: '110px minmax(0, 1fr)', gap: '6px 10px' }}>
            {detail.facts.map((fact) => [
              <span key={`${fact.label}-label`} style={{ color: 'var(--text-tertiary)' }}>
                {fact.label}
              </span>,
              <span key={`${fact.label}-value`} style={{ overflowWrap: 'anywhere', fontFamily: fact.mono ? 'var(--font-mono)' : 'inherit', color: 'var(--text-primary)' }}>
                {fact.value}
              </span>,
            ])}
          </div>
          {detail.drafter.shown ? (
            <>
              <h3 className="lbl" style={{ margin: '20px 0 8px' }}>
                DRAFTER
              </h3>
              {detail.drafter.paired ? (
                <>
                  <div className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-primary)', overflowWrap: 'anywhere' }}>
                    {detail.drafter.path}
                  </div>
                  {detail.drafter.ok ? (
                    <>
                      <p style={{ margin: '6px 0 0', color: 'var(--text-secondary)' }}>{detail.drafter.said}</p>
                      <p style={{ margin: '4px 0 0', color: 'var(--text-tertiary)' }}>{detail.drafter.line}</p>
                    </>
                  ) : (
                    <p style={{ display: 'flex', alignItems: 'flex-start', gap: 6, margin: '6px 0 0', color: 'var(--status-warning)', overflowWrap: 'anywhere' }}>
                      <Icon name="error" size={13} style={{ marginTop: 2 }} />
                      <span>{detail.drafter.said}</span>
                    </p>
                  )}
                  <button type="button" className="btn-g" onClick={() => void model.unpair()} title={detail.drafter.unpairTip} style={{ marginTop: 10, height: 26, fontSize: 'var(--t-small)' }}>
                    Unpair
                  </button>
                </>
              ) : null}
              {detail.drafter.canPair ? (
                <>
                  <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{detail.drafter.pairNone}</p>
                  <button type="button" className="btn-g" onClick={() => void model.askToPairDrafter()} title={detail.drafter.pairTip} style={{ marginTop: 10, height: 26, fontSize: 'var(--t-small)' }}>
                    Pair a drafter…
                  </button>
                </>
              ) : null}
            </>
          ) : null}
          {detail.adapters.shown ? (
            <>
              <h3 className="lbl" style={{ margin: '20px 0 8px' }}>
                ADAPTERS
              </h3>
              {detail.adapters.none ? <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{detail.adapters.noneText}</p> : null}
              {detail.adapters.rows.map((row) => (
                <div key={row.path} style={{ padding: '6px 0', borderBottom: '1px solid var(--line-border)' }}>
                  <div className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-primary)', overflowWrap: 'anywhere' }}>
                    {row.path}
                  </div>
                  <div className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>
                    {row.scale}
                  </div>
                  <div style={{ color: row.color, overflowWrap: 'anywhere' }}>{row.detail}</div>
                </div>
              ))}
              <button type="button" className="btn-g" onClick={() => model.editAdapters()} title={detail.adapters.tip} style={{ marginTop: 10, height: 26, fontSize: 'var(--t-small)' }}>
                Edit adapters…
              </button>
            </>
          ) : null}
          {detail.lineage.length > 0 ? (
            <>
              <h3 className="lbl" style={{ margin: '20px 0 10px' }}>
                LINEAGE
              </h3>
              <div style={{ display: 'flex', flexDirection: 'column' }}>
                {detail.lineage.map((step) => (
                  <div key={step.label + step.how} style={{ display: 'grid', gridTemplateColumns: '14px minmax(0, 1fr)', columnGap: 10 }}>
                    <span aria-hidden="true" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
                      <span style={{ width: 9, height: 9, marginTop: 4, boxSizing: 'border-box', borderRadius: 'var(--radius-dot)', border: `1.5px solid ${step.dot}`, background: step.fill }} />
                      <span style={{ flex: '1 1 auto', width: 1, minHeight: 14, background: step.line }} />
                    </span>
                    <div style={{ paddingBottom: 12, minWidth: 0 }}>
                      {step.isLink ? (
                        <button type="button" onClick={() => void model.store.select(step.label).then(() => model.pick('store'))} className="mono" style={{ padding: 0, border: 0, background: 'transparent', fontSize: 'var(--t-ui)', color: 'var(--status-accent)', textAlign: 'left', textDecoration: 'underline', textDecorationColor: 'var(--line-strong)', textUnderlineOffset: 3, overflowWrap: 'anywhere' }}>
                          {step.label}
                        </button>
                      ) : (
                        <span className="mono" style={{ fontSize: 'var(--t-ui)', color: 'var(--text-primary)', overflowWrap: 'anywhere' }}>
                          {step.label}
                        </span>
                      )}
                      <div style={{ color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{step.how}</div>
                    </div>
                  </div>
                ))}
              </div>
            </>
          ) : null}
          {detail.files.length > 0 ? (
            <>
              <h3 className="lbl" style={{ margin: '12px 0 8px' }}>
                FILES
              </h3>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                {detail.files.map((file) => (
                  <div key={file.path} style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: 10 }}>
                    <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
                      {file.path}
                    </span>
                    <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                      {file.size}
                    </span>
                  </div>
                ))}
              </div>
            </>
          ) : null}
        </aside>
      ) : null}
    </div>
  )
}

/** Roles: which model does which job, the six the CLI resolves, each picker moving its role through its verb. */
export function RolesTab({ model }: { readonly model: ModelsViewModel }) {
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, overflowY: 'auto', padding: '20px 24px 28px' }}>
      <div style={{ maxWidth: 820 }}>
        <div style={{ margin: '0 0 16px' }}>
          <p style={{ margin: 0, fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }}>Which model does which job. Helpers run beside the chat model; an unset role falls back as shown, and the turn says when a helper did the work.</p>
          <RolesNotes model={model} />
        </div>
        <RoleRows model={model} />
      </div>
    </div>
  )
}

/**
 * The roles' notes: the suite they resolve under, and why they could not be
 * read -- what Roles draws under its intro, and Settings › Models & roles
 * under its own (design 1i, through the slot the app root fills).
 */
export function RolesNotes({ model, margin = '6px 0 0' }: { readonly model: ModelsViewModel; readonly margin?: string }) {
  useObserved(model)
  const face = model.face().roles
  return (
    <>
      {face.suiteLine !== '' ? <p style={{ margin, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{face.suiteLine}</p> : null}
      {face.failure !== '' ? <p style={{ margin, fontSize: 'var(--t-small)', color: 'var(--status-error)' }}>{`The roles could not be read: ${face.failure}`}</p> : null}
    </>
  )
}

/** The six roles' rows, each picker moving its role through its verb: Roles', and Settings › Models & roles' (design 1i), its picker a little narrower there, as the old pane draws it. */
export function RoleRows({ model, picker = 250 }: { readonly model: ModelsViewModel; readonly picker?: number }) {
  useObserved(model)
  const face = model.face().roles
  return face.rows.map((row) => (
    <div key={row.key} style={{ display: 'grid', gridTemplateColumns: `150px minmax(0, 1fr) ${picker}px`, gap: '12px 20px', alignItems: 'center', padding: '14px 0', borderBottom: '1px solid var(--line-border)' }}>
      <div>
        <div style={{ fontSize: 'var(--t-body)', fontWeight: 600 }}>{row.label}</div>
        <div className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
          {row.key}
        </div>
      </div>
      <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
        {row.desc}
        <div style={{ marginTop: 2, color: row.noteColor }}>{row.note}</div>
      </div>
      <select className="inp" value={row.value} onChange={(event) => void model.assign(row.key, event.target.value)} aria-label={`Model for ${row.label}`} style={{ width: '100%' }}>
        {row.options.map((option) => (
          <option key={option.value} value={option.value} disabled={option.value === ''}>
            {option.label}
          </option>
        ))}
      </select>
    </div>
  ))
}

/** The pair sheet's content: exactly what the binary's completion offers, the drafter's own line, the command as typed. */
export function PairDrafterSheet({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  const sheet = model.face().pairSheet
  return (
    <>
      <p style={{ margin: 0, color: 'var(--text-secondary)' }}>A drafter proposes the next tokens and its base checks them, so every answer is still the base’s own. It runs beside this backend through mlx-vlm’s speculative path.</p>
      {sheet.warning !== '' ? <p style={{ margin: 0, color: 'var(--status-warning)' }}>{sheet.warning}</p> : null}
      {sheet.offers.length > 0 ? (
        <div role="radiogroup" aria-label="Drafter" style={{ display: 'flex', flexDirection: 'column', border: '1px solid var(--line-border)', borderRadius: 'var(--radius-card)', overflow: 'hidden' }}>
          {sheet.offers.map((offer) => (
            <button
              key={offer.handle}
              type="button"
              role="radio"
              aria-checked={offer.current}
              onClick={() => model.choosePairing(offer.handle)}
              className="hv"
              style={{ display: 'grid', gridTemplateColumns: '14px minmax(0, 1fr)', columnGap: 10, alignItems: 'center', minHeight: 40, padding: '6px 12px', border: 0, borderBottom: '1px solid var(--line-border)', background: offer.current ? 'var(--surface-selection)' : 'transparent', color: 'var(--text-primary)', textAlign: 'left' }}
            >
              <span aria-hidden="true" style={{ width: 12, height: 12, boxSizing: 'border-box', borderRadius: 'var(--radius-dot)', border: offer.current ? '4px solid var(--status-accent)' : '1.5px solid var(--line-strong)', background: offer.current ? 'var(--surface-card)' : 'transparent' }} />
              <span style={{ fontSize: 'var(--t-body)', overflowWrap: 'anywhere' }}>{offer.handle}</span>
            </button>
          ))}
        </div>
      ) : null}
      {sheet.line !== '' ? <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{sheet.line}</p> : null}
      {sheet.command !== '' ? (
        <div className="mono" style={{ padding: '8px 10px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
          {sheet.command}
        </div>
      ) : null}
    </>
  )
}
