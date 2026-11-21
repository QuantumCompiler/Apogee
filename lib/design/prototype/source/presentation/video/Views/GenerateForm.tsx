import { useObserved } from '../../styles/Observation/useObserved'
import type { FormPreset, SeedMode } from '../ViewModels/VideoForm'
import type { VideoViewModel } from '../ViewModels/VideoViewModel'
import { Icon } from '../../styles/Components/Icon'

const label = { display: 'flex', flexDirection: 'column', gap: 6, color: 'var(--text-secondary)' } as const
const segment = (current: boolean) => ({ background: current ? 'var(--surface-control)' : 'transparent', color: current ? 'var(--text-primary)' : 'var(--text-secondary)', fontWeight: current ? 600 : 400 })
const small = { height: 26, fontSize: 'var(--t-small)' } as const
const close = (
  <Icon name="close" size={11} />
)

/**
 * The generate form (design 1k, the old `vid.f`): a new clip, a record run
 * again or a clip continued -- what the CLI will say of it, the prompt, the
 * start, the pipeline, the quality, the length, rate and steps, the frame,
 * the seed, the LoRAs, the file, the run's facts, the exact command, and
 * Generate.
 */
export function GenerateForm({ model }: { readonly model: VideoViewModel }) {
  useObserved(model)
  const form = model.face().form
  return (
    <aside className="ap-detail" aria-label="Generate a clip" style={{ flex: '0 0 360px', minHeight: 0, overflowY: 'auto', padding: '16px 20px 28px', background: 'var(--surface-inspector)', borderLeft: '1px solid var(--line-border)', fontSize: 'var(--t-small)' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <h2 style={{ flex: '1 1 auto', minWidth: 0, margin: 0, fontSize: 'var(--t-h3)', fontWeight: 600, overflowWrap: 'anywhere' }}>{form.heading}</h2>
        {form.notNew ? (
          <button type="button" className="btn-g" onClick={() => model.startFresh()} style={small}>
            Start fresh
          </button>
        ) : null}
      </div>
      {form.told === '' ? null : (
        <div className="mono" style={{ marginTop: 10, padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
          {form.told}
        </div>
      )}
      <label style={{ ...label, marginTop: 14 }}>
        Prompt
        <textarea rows={4} value={form.prompt} onChange={(event) => model.edit({ prompt: event.target.value })} placeholder="Say what the clip shows" style={{ boxSizing: 'border-box', width: '100%', resize: 'vertical', padding: '8px 10px', border: '1px solid var(--line-strong)', borderRadius: 6, background: 'var(--surface-card)', fontSize: 'var(--t-body)', lineHeight: 1.5, color: 'var(--text-primary)' }} />
      </label>
      <div style={{ marginTop: 12 }}>
        <div style={{ marginBottom: 6, color: 'var(--text-secondary)' }}>Start from</div>
        {form.continuing ? (
          <span className="chip" style={{ paddingRight: 10 }}>
            <Icon name="camera" size={13} style={{ color: 'var(--text-secondary)' }} />
            <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
              the last frame of {form.continues}
            </span>
          </span>
        ) : null}
        {form.image === '' ? null : (
          <span className="chip">
            <Icon name="picture" size={13} style={{ color: 'var(--text-secondary)' }} />
            <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
              {form.image}
            </span>
            <button type="button" className="ib" onClick={() => model.dropImage()} aria-label="Remove the seed image" style={{ width: 22, height: 22 }}>
              {close}
            </button>
          </span>
        )}
        {form.canAddImage ? (
          <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8 }}>
            <span style={{ color: 'var(--text-primary)' }}>Text only</span>
            <button type="button" className="btn-g" onClick={() => model.addImage()} style={small}>
              Add a seed image…
            </button>
          </div>
        ) : null}
      </div>
      <label style={{ ...label, marginTop: 12 }}>
        Pipeline
        <select className="inp" value={form.pipeline} onChange={(event) => model.edit({ pipeline: event.target.value })} disabled={form.lockFrame} style={{ width: '100%' }}>
          {form.pipelines.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
      </label>
      <div style={{ marginTop: 12 }}>
        <div style={{ marginBottom: 6, color: 'var(--text-secondary)' }}>Quality</div>
        <div role="group" aria-label="Quality" className="seg" style={{ display: 'inline-flex' }}>
          {form.presets.map((option) => (
            <button key={option.value} type="button" aria-pressed={form.preset === option.value} onClick={() => model.setPreset(option.value as FormPreset)} style={segment(form.preset === option.value)}>
              {option.label}
            </button>
          ))}
        </div>
        <div style={{ marginTop: 6, color: 'var(--text-tertiary)' }}>{form.presetNote}</div>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, minmax(0, 1fr))', gap: 10, marginTop: 12 }}>
        <label style={label}>
          Seconds
          <input type="number" className="inp mono" min="0.5" max="10" step="0.5" value={form.dur} onChange={(event) => model.edit({ dur: event.target.value })} style={{ width: '100%' }} />
        </label>
        <label style={label}>
          Frame rate
          <input type="number" className="inp mono" min="4" max="60" step="1" value={form.fps} onChange={(event) => model.edit({ fps: event.target.value })} disabled={form.lockFrame} style={{ width: '100%' }} />
        </label>
        <label style={label}>
          Steps
          <input type="number" className="inp mono" min="1" max="100" step="1" value={form.steps} onChange={(event) => model.setSteps(event.target.value)} style={{ width: '100%' }} />
        </label>
      </div>
      <label style={{ ...label, marginTop: 10 }}>
        Frame size
        <select className="inp mono" value={form.size} onChange={(event) => model.edit({ size: event.target.value })} disabled={form.lockFrame} style={{ width: '100%' }}>
          {form.sizes.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
      </label>
      <div className="mono" style={{ marginTop: 6, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
        {form.framesText}
      </div>
      <div style={{ marginTop: 12 }}>
        <div style={{ marginBottom: 6, color: 'var(--text-secondary)' }}>Seed</div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <div role="group" aria-label="Seed" className="seg" style={{ flex: '0 0 auto' }}>
            {form.seedModes.map((option) => (
              <button key={option.value} type="button" aria-pressed={form.seedMode === option.value} onClick={() => model.setSeedMode(option.value as SeedMode)} className="mono" style={{ ...segment(form.seedMode === option.value), fontSize: 'var(--t-ui)' }}>
                {option.label}
              </button>
            ))}
          </div>
          {form.seedGiven ? <input type="text" inputMode="numeric" className="inp mono" value={form.seed} onChange={(event) => model.edit({ seed: event.target.value })} aria-label="Seed" style={{ flex: '1 1 auto', minWidth: 0, width: '100%' }} /> : null}
        </div>
        <div style={{ marginTop: 6, color: 'var(--text-tertiary)' }}>{form.seedNote}</div>
      </div>
      <div style={{ marginTop: 14 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 6, color: 'var(--text-secondary)' }}>
          <span>LoRAs</span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            {form.loraCount}
          </span>
        </div>
        {form.loras.map((entry, index) => (
          <div key={entry.path} style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) 64px 24px', gap: 8, alignItems: 'center', marginBottom: 6 }}>
            <div style={{ minWidth: 0 }}>
              <div className="mono" title={entry.path} style={{ fontSize: 'var(--t-ui)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                {entry.name}
              </div>
              <div className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
                {entry.state}
              </div>
            </div>
            <input type="number" className="inp mono" min="0.05" max="2" step="0.05" value={entry.scale} onChange={(event) => model.setLoraScale(index, event.target.value)} aria-label={`Strength of ${entry.name}`} style={{ width: '100%' }} />
            <button type="button" className="ib" onClick={() => model.removeLora(index)} aria-label={`Remove ${entry.name}`} style={{ width: 24, height: 24 }}>
              {close}
            </button>
          </div>
        ))}
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
          <button type="button" className="btn-g" onClick={() => model.addLora()} disabled={form.loraFull} style={small}>
            Add a LoRA file…
          </button>
        </div>
        <div style={{ marginTop: 6, color: 'var(--text-tertiary)' }}>Your own .safetensors files: up to 4, each at a strength above 0 and up to 2. Apogee never downloads one, and records each by its sha256.</div>
      </div>
      <label style={{ ...label, marginTop: 14 }}>
        Save as
        <input type="text" className="inp mono" value={form.out} onChange={(event) => model.edit({ out: event.target.value })} style={{ width: '100%' }} />
      </label>
      <div style={{ marginTop: 6, color: 'var(--text-tertiary)' }}>In ~/Movies/Apogee, its record beside it as {form.out}.json. A generation never writes over a file.</div>
      {form.errors.length > 0 ? (
        <div role="alert" style={{ marginTop: 12, display: 'flex', flexDirection: 'column', gap: 4, padding: '10px 12px', border: '1px solid var(--status-error)', borderRadius: 6, color: 'var(--text-primary)' }}>
          {form.errors.map((error) => (
            <span key={error} style={{ display: 'flex', gap: 8 }}>
              <span aria-hidden="true" style={{ flex: '0 0 auto', width: 6, height: 6, marginTop: 7, borderRadius: '50%', background: 'var(--status-error)' }} />
              <span>{error}</span>
            </span>
          ))}
        </div>
      ) : null}
      <div style={{ display: 'grid', gridTemplateColumns: '64px minmax(0, 1fr)', gap: '6px 10px', marginTop: 14, paddingTop: 12, borderTop: '1px solid var(--line-border)' }}>
        <span style={{ color: 'var(--text-tertiary)' }}>Runs on</span>
        <span>{form.runsOn}</span>
        <span style={{ color: 'var(--text-tertiary)' }}>Expect</span>
        <span>{form.expect}</span>
        <span style={{ color: 'var(--text-tertiary)' }}>Memory</span>
        <span>{form.memory}</span>
      </div>
      <div className="mono" style={{ marginTop: 12, padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
        {form.command}
      </div>
      <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8, marginTop: 14 }}>
        <button type="button" className="btn-p" onClick={() => model.generate()} disabled={!form.canGenerate} style={{ height: 32, padding: '0 16px' }}>
          Generate
        </button>
        <span style={{ color: 'var(--text-tertiary)' }}>{form.goNote}</span>
      </div>
    </aside>
  )
}
