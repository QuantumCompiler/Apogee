import { useObserved } from '../../styles/Observation/useObserved'
import { ConvertForm, QuantizeForm, RegisterForm } from '../ViewModels/StoreForms'
import { Estimate } from '../ViewModels/StoreForms'
import type { StoreViewModel } from '../ViewModels/StoreViewModel'
import { DeleteNarrationCard } from './DeleteNarrationCard'

const columns = 'minmax(190px, 1fr) 100px 92px 72px minmax(170px, 210px)'
const link = { padding: 0, border: 0, background: 'transparent', fontSize: 'var(--t-ui)', color: 'var(--status-accent)', textAlign: 'left', textDecoration: 'underline', textDecorationColor: 'var(--line-strong)', textUnderlineOffset: 3, overflowWrap: 'anywhere' } as const
const code = { padding: '8px 10px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' } as const

/**
 * Stored weights (design 1h): the old prototype's tab (`main.html` at
 * `b375fac9`, `mdl.isStore`, 959-1029) -- the summary and Register all, the
 * sets grouped by model, the fold, and one set's details -- over the Mac
 * app's `StoreViewModel`. Its `StoreView`.
 */
export function StoreView({ model }: { readonly model: StoreViewModel }) {
  useObserved(model)
  const face = model.face()
  const detail = face.detail
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <div style={{ flex: '1 1 0', minWidth: 0, overflow: 'auto', padding: '12px 24px 24px' }}>
        {face.narration.shown ? <DeleteNarrationCard title={face.narration.title} text={face.narration.text} dismiss={() => model.dismissNarration()} /> : null}
        {face.failure !== '' ? (
          <p role="alert" style={{ margin: '0 0 10px', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>
            {`That didn’t work: ${face.failure}`}
          </p>
        ) : null}
        <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: '8px 12px', minHeight: 32, marginBottom: 4 }}>
          <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{face.summary}</span>
          <span style={{ flex: '1 1 auto' }} />
          {face.registerAll.shown ? (
            <button type="button" className="btn" onClick={() => void model.askToRegisterAll()} title="apogee config scan --register">
              {face.registerAll.label}
            </button>
          ) : null}
        </div>
        <div className="lbl" style={{ display: 'grid', gridTemplateColumns: columns, columnGap: 12, alignItems: 'center', height: 34, borderBottom: '1px solid var(--line-strong)' }}>
          <span>SET</span>
          <span>FORMAT</span>
          <span>PRECISION</span>
          <span style={{ textAlign: 'right' }}>ON DISK</span>
          <span>STATE</span>
        </div>
        {face.groups.map((group) => (
          <div key={group.dir}>
            <div style={{ display: 'flex', alignItems: 'baseline', flexWrap: 'wrap', gap: '4px 12px', padding: '16px 0 6px' }}>
              <span style={{ fontSize: 'var(--t-body)', fontWeight: 600 }}>{group.dir}</span>
              <span className="mono" style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                {group.origin}
              </span>
              <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                {group.summary}
              </span>
            </div>
            {group.sets.map((row) => (
              <button
                key={row.handle}
                type="button"
                onClick={() => void model.toggle(row.handle)}
                aria-current={row.current}
                className="hv"
                style={{ width: '100%', display: 'grid', gridTemplateColumns: columns, columnGap: 12, alignItems: 'center', minHeight: 44, padding: '4px 8px', margin: '0 -8px', boxSizing: 'content-box', border: 0, borderTop: '1px solid var(--line-border)', borderRadius: 0, background: row.current ? 'var(--surface-selection)' : '', color: 'var(--text-primary)', textAlign: 'left', fontSize: 'var(--t-body)' }}
              >
                <span style={{ minWidth: 0, display: 'flex', flexDirection: 'column', lineHeight: 1.35 }}>
                  <span className="mono" style={{ fontSize: 'var(--t-ui)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                    {row.short}
                  </span>
                  {row.fromText !== '' ? (
                    <span className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                      {row.fromText}
                    </span>
                  ) : null}
                </span>
                <span className="mono" style={{ justifySelf: 'start', padding: '1px 6px', borderRadius: 4, background: 'var(--surface-control)', fontSize: 'var(--t-label)', color: row.formatColor }}>
                  {row.format}
                </span>
                <span className="mono" style={{ fontSize: 'var(--t-ui)', color: 'var(--text-secondary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                  {row.level}
                </span>
                <span className="mono" style={{ fontSize: 'var(--t-ui)', textAlign: 'right', color: 'var(--text-secondary)' }}>
                  {row.size}
                </span>
                <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, minWidth: 0, fontSize: 'var(--t-small)', color: row.stateColor }}>
                  <span className={row.busy ? 'pulse' : undefined} style={{ flex: '0 0 auto', width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: row.dot }} />
                  <span style={{ whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{row.state}</span>
                </span>
              </button>
            ))}
          </div>
        ))}
        {face.fold.shown ? (
          <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: '8px 12px', marginTop: 14, paddingTop: 12, borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>
            <span>{face.fold.text}</span>
            <button type="button" className="btn-g" onClick={() => void model.setShowingAll(!model.showingAll)} style={{ height: 26, fontSize: 'var(--t-small)' }}>
              {face.fold.button}
            </button>
          </div>
        ) : null}
        <p style={{ margin: '14px 0 0', maxWidth: 760, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>
          Every set in ~/.apogee/models, read with <span className="mono">models list</span> and <span className="mono">models info</span>. SafeTensors weights don’t run as they are: convert them to GGUF for llama.cpp, or to MLX for Apple silicon. A GGUF or MLX set runs once it is registered as a backend.
        </p>
      </div>
      {detail.shown ? (
        <aside className="ap-detail" aria-label="Stored set" style={{ flex: '0 0 380px', minHeight: 0, overflowY: 'auto', background: 'var(--surface-inspector)', borderLeft: '1px solid var(--line-border)', padding: '18px 20px 24px', fontSize: 'var(--t-small)' }}>
          <div style={{ display: 'flex', alignItems: 'flex-start', gap: 8 }}>
            <div style={{ flex: '1 1 auto', minWidth: 0 }}>
              <div className="lbl">{detail.kicker}</div>
              <h2 className="mono" style={{ margin: '4px 0 0', fontSize: 'var(--t-title)', lineHeight: 1.35, fontWeight: 600, overflowWrap: 'anywhere' }}>
                {detail.handle}
              </h2>
              <div style={{ display: 'inline-flex', alignItems: 'center', gap: 6, marginTop: 6, color: detail.stateColor }}>
                <span style={{ width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: detail.dot }} />
                {detail.state}
              </div>
            </div>
            <button type="button" className="ib" onClick={() => model.close()} aria-label="Close details">
              <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
                <path d="M4.5 4.5l7 7M11.5 4.5l-7 7" />
              </svg>
            </button>
          </div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, margin: '16px 0 6px' }}>
            {detail.actions.map((action) => (
              <button key={action.action} type="button" className={action.kind} onClick={() => model.act(action.action)} disabled={action.disabled} title={action.title}>
                {action.label}
              </button>
            ))}
          </div>
          {detail.note !== '' ? <p style={{ margin: '10px 0 0', color: 'var(--text-secondary)' }}>{detail.note}</p> : null}
          <h3 className="lbl" style={{ margin: '20px 0 8px' }}>
            FACTS
          </h3>
          <div style={{ display: 'grid', gridTemplateColumns: '96px minmax(0, 1fr)', gap: '6px 10px' }}>
            {detail.facts.map((fact) => [
              <span key={`${fact.label}-label`} style={{ color: 'var(--text-tertiary)' }}>
                {fact.label}
              </span>,
              <span key={`${fact.label}-value`} style={{ overflowWrap: 'anywhere', fontFamily: fact.mono ? 'var(--font-mono)' : 'inherit', color: 'var(--text-primary)' }}>
                {fact.value}
              </span>,
            ])}
          </div>
          {detail.voices.length > 0 ? (
            <>
              <h3 className="lbl" style={{ margin: '20px 0 6px' }}>{`VOICES BY LANGUAGE · ${detail.voices.reduce((sum, voice) => sum + Number(voice.count), 0)}`}</h3>
              {detail.voices.map((voice) => (
                <div key={voice.lang} style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: 10, padding: '5px 0', borderBottom: '1px solid var(--line-border)' }}>
                  <span>{voice.lang}</span>
                  <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{`${voice.count} · ${voice.names}`}</span>
                </div>
              ))}
            </>
          ) : null}
          {detail.parents.length > 0 ? (
            <>
              <h3 className="lbl" style={{ margin: '20px 0 8px' }}>
                MADE FROM
              </h3>
              {detail.parents.map((parent) => (
                <div key={parent.label} style={{ padding: '4px 0' }}>
                  {parent.isLink ? (
                    <button type="button" className="mono" onClick={() => void model.select(parent.label)} style={link}>
                      {parent.label}
                    </button>
                  ) : (
                    <span className="mono" style={{ fontSize: 'var(--t-ui)', overflowWrap: 'anywhere' }}>
                      {parent.label}
                    </span>
                  )}
                  <div style={{ color: 'var(--text-secondary)' }}>{parent.how}</div>
                </div>
              ))}
            </>
          ) : null}
          {detail.children.length > 0 ? (
            <>
              <h3 className="lbl" style={{ margin: '16px 0 8px' }}>
                MADE FROM IT
              </h3>
              {detail.children.map((child) => (
                <div key={child.label} style={{ padding: '4px 0' }}>
                  <button type="button" className="mono" onClick={() => void model.select(child.label)} style={link}>
                    {child.label}
                  </button>
                  <div style={{ color: 'var(--text-secondary)' }}>{child.how}</div>
                </div>
              ))}
            </>
          ) : null}
          <h3 className="lbl" style={{ margin: '20px 0 8px' }}>{`THE MODEL · ${detail.dir}`}</h3>
          <p style={{ margin: '0 0 8px', color: 'var(--text-secondary)' }}>{detail.dirSummary}</p>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
            <button type="button" className="btn-g" onClick={() => model.askToRename(detail.dir)}>
              Rename…
            </button>
            <button type="button" className="btn-g" onClick={() => void model.askToDeleteModel(detail.dir)}>
              Delete all of it…
            </button>
          </div>
          <h3 className="lbl" style={{ margin: '20px 0 8px' }}>
            ON THE COMMAND LINE
          </h3>
          <div className="mono" style={code}>
            {detail.command}
          </div>
        </aside>
      ) : null}
    </div>
  )
}

/** A segmented choice, as the old prototype's `seg`. */
function Segments({ label, options, current, pick }: { readonly label: string; readonly options: ReadonlyArray<readonly [string, string]>; readonly current: string; readonly pick: (value: string) => void }) {
  return (
    <div role="group" aria-label={label} className="seg" style={{ display: 'inline-flex' }}>
      {options.map(([value, words]) => (
        <button key={value} type="button" aria-pressed={current === value} onClick={() => pick(value)} style={{ background: current === value ? 'var(--surface-control)' : 'transparent', color: current === value ? 'var(--text-primary)' : 'var(--text-secondary)', fontWeight: current === value ? 600 : 400 }}>
          {words}
        </button>
      ))}
    </div>
  )
}

const fieldLabel = { display: 'flex', flexDirection: 'column', gap: 6, color: 'var(--text-secondary)' } as const

function ConvertFields({ model, form }: { readonly model: StoreViewModel; readonly form: ConvertForm }) {
  const chosen = form.levelChoices.filter((level) => form.levels.includes(level))
  const names = form.names
  return (
    <>
      <div>
        <div style={{ marginBottom: 6, color: 'var(--text-secondary)' }}>Make</div>
        <Segments label="Make" options={[['gguf', 'GGUF · llama.cpp'], ['mlx', 'MLX · Apple silicon']]} current={form.mlx ? 'mlx' : 'gguf'} pick={(value) => model.retarget(value === 'mlx')} />
      </div>
      <label style={fieldLabel}>
        Precision
        <select className="inp mono" value={form.written} onChange={(event) => model.editConvert((edited) => edited.setPrecision(event.target.value))} style={{ width: 240 }}>
          {form.precisions.map((precision) => (
            <option key={precision} value={precision}>
              {precision === form.defaultPrecision ? `${precision}, the default` : precision === 'auto' ? 'auto, as the weights are' : precision}
            </option>
          ))}
        </select>
      </label>
      <div>
        <div style={{ marginBottom: 6, color: 'var(--text-secondary)' }}>{form.mlx ? 'Also make, one conversion each' : 'Then quantize to'}</div>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
          {form.levelChoices.map((level) => {
            const on = form.levels.includes(level)
            return (
              <button key={level} type="button" aria-pressed={on} onClick={() => model.editConvert((edited) => edited.toggleLevel(level))} className="mono" style={{ height: 28, padding: '0 10px', border: `1px solid ${on ? 'var(--status-accent)' : 'var(--line-border)'}`, borderRadius: 'var(--radius-control)', background: on ? 'var(--surface-selection)' : 'transparent', color: 'var(--text-primary)', fontSize: 'var(--t-ui)' }}>
                {level}
              </button>
            )
          })}
        </div>
        <div style={{ marginTop: 6, color: 'var(--text-tertiary)' }}>{chosen.length > 0 ? 'Each is its own set and its own backend, so choosing one registers what’s made.' : form.mlx ? 'Optional: further MLX builds at other precisions.' : 'Optional: smaller copies, made from the GGUF this makes.'}</div>
      </div>
      <div>
        <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8, cursor: 'pointer', color: 'var(--text-primary)' }}>
          <input type="checkbox" checked={form.register || chosen.length > 0} disabled={form.registerForced || form.alone} onChange={() => model.editConvert((edited) => (edited.register = !edited.register))} style={{ accentColor: 'var(--status-accent)' }} />
          Register what’s made as backends
        </label>
        {names.length > 0 ? (
          <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8, marginTop: 8, color: 'var(--text-secondary)' }}>
            <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
              Names begin
              <input type="text" className="inp mono" value={form.baseName} onChange={(event) => model.editConvert((edited) => (edited.baseName = event.target.value))} style={{ width: 220 }} />
            </label>
            <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{`as ${names.join(', ')}`}</span>
          </div>
        ) : null}
      </div>
      {form.mlx ? (
        <>
          <label style={fieldLabel}>
            <span>
              Convert as type <span style={{ color: 'var(--text-tertiary)' }}>optional</span>
            </span>
            <input type="text" className="inp mono" value={form.modelType} onChange={(event) => model.editConvert((edited) => (edited.modelType = event.target.value))} placeholder="the architecture mlx-lm reads from the weights" style={{ width: '100%' }} />
          </label>
          <div style={{ marginTop: -8, color: 'var(--text-tertiary)' }}>Only for a vendor’s variant that mlx-lm’s converter doesn’t list. The weights are untouched, and the record says it was converted as that type.</div>
        </>
      ) : null}
    </>
  )
}

function QuantizeFields({ model, form }: { readonly model: StoreViewModel; readonly form: QuantizeForm }) {
  return (
    <>
      <label style={fieldLabel}>
        Level
        <select className="inp mono" value={form.level} onChange={(event) => model.editQuantize((edited) => edited.choose(event.target.value))} style={{ width: 240 }}>
          {QuantizeForm.levels.map((level) => {
            const about = Estimate.size(level, form.bytes)
            return (
              <option key={level} value={level}>
                {`${level}${level === 'Q4_K_M' ? ', the default' : ''}${about ? ` · ${about}` : ''}`}
              </option>
            )
          })}
        </select>
      </label>
      <div>
        <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8, cursor: 'pointer', color: 'var(--text-primary)' }}>
          <input type="checkbox" checked={form.register} onChange={() => model.editQuantize((edited) => (edited.register = !edited.register))} style={{ accentColor: 'var(--status-accent)' }} />
          Register it as a backend when it’s made
        </label>
        {form.register ? (
          <div style={{ marginTop: 8 }}>
            <input type="text" className="inp mono" value={form.name} onChange={(event) => model.editQuantize((edited) => edited.rename(event.target.value))} aria-label="Backend name" style={{ width: 280 }} />
          </div>
        ) : null}
      </div>
    </>
  )
}

function RegisterFields({ model, form }: { readonly model: StoreViewModel; readonly form: RegisterForm }) {
  return (
    <>
      <label style={fieldLabel}>
        Backend name
        <input type="text" className="inp mono" value={form.name} onChange={(event) => model.editRegister((edited) => (edited.name = event.target.value))} style={{ width: 320 }} />
      </label>
      <div style={{ display: 'grid', gridTemplateColumns: '110px minmax(0, 1fr)', gap: '10px 12px', alignItems: 'center' }}>
        <span style={{ color: 'var(--text-secondary)' }}>Runs on</span>
        <span>{form.type === 'mlx' ? 'MLX, in a Python child process' : 'llama.cpp, in the session’s own process'}</span>
        <span style={{ color: 'var(--text-secondary)' }}>Window</span>
        <select className="inp mono" value={form.window} onChange={(event) => model.editRegister((edited) => (edited.window = event.target.value))} aria-label="Context window" style={{ width: 240 }}>
          {form.windowChoices.map((choice) => (
            <option key={choice.value} value={choice.value}>
              {choice.label}
            </option>
          ))}
        </select>
        {form.type === 'llamacpp' ? (
          <>
            <span style={{ color: 'var(--text-secondary)' }}>Attention cache</span>
            <span style={{ justifySelf: 'start' }}>
              <Segments label="Attention cache" options={RegisterForm.caches.map((cache) => [cache, cache] as const)} current={form.cache} pick={(value) => model.editRegister((edited) => (edited.cache = value))} />
            </span>
          </>
        ) : null}
        {form.projector !== undefined ? (
          <>
            <span style={{ color: 'var(--text-secondary)' }}>Projector</span>
            <span>
              Its <span className="mono">mmproj</span> file goes with it, so the backend can read images.
            </span>
          </>
        ) : null}
      </div>
    </>
  )
}

/**
 * A store sheet's content (design 1h): the old prototype's `ui.storeSheetOpen`
 * (`main.html` at `b375fac9`, 2405-2451) -- its source and intro, its fields
 * by kind (convert, quantize, register, rename), the steps, the notes, what
 * stops it, and the command as typed. The frame and its buttons are the
 * shell's host's.
 */
export function StoreSheet({ model }: { readonly model: StoreViewModel }) {
  useObserved(model)
  const sheet = model.sheet()
  if (!sheet) return null
  return (
    <>
      <p className="mono" style={{ margin: 0, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
        {sheet.source}
      </p>
      <p style={{ margin: 0, color: 'var(--text-secondary)' }}>{sheet.intro}</p>
      {model.convertForm ? <ConvertFields model={model} form={model.convertForm} /> : null}
      {model.quantizeForm ? <QuantizeFields model={model} form={model.quantizeForm} /> : null}
      {model.register ? <RegisterFields model={model} form={model.register} /> : null}
      {model.renaming ? (
        <>
          <label style={fieldLabel}>
            New name
            <input type="text" className="inp mono" value={model.renaming.name} onChange={(event) => model.editRename((edited) => (edited.name = event.target.value))} style={{ width: 320 }} />
          </label>
        </>
      ) : null}
      {sheet.steps.length > 0 ? (
        <ol style={{ margin: 0, padding: '12px 14px 12px 34px', border: '1px solid var(--line-border)', borderRadius: 'var(--radius-card)', background: 'var(--surface-window)', display: 'flex', flexDirection: 'column', gap: 4, color: 'var(--text-secondary)' }}>
          {sheet.steps.map((step) => (
            <li key={step}>{step}</li>
          ))}
        </ol>
      ) : null}
      {sheet.notes.map((note) => (
        <p key={note} style={{ margin: 0, color: sheet.kind === 'rename' ? 'var(--text-secondary)' : 'var(--text-tertiary)' }}>
          {note}
        </p>
      ))}
      {sheet.errors.length > 0 ? (
        <div role="alert" style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: '10px 12px', border: '1px solid var(--status-error)', borderRadius: 'var(--radius-control)' }}>
          {sheet.errors.map((error) => (
            <span key={error}>{error}</span>
          ))}
        </div>
      ) : null}
      <div className="mono" style={code}>
        {sheet.command}
      </div>
    </>
  )
}

