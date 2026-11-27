import { useObserved } from '../../styles/Observation/useObserved'
import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'
import { Icon } from '../../styles/Components/Icon'

const label = { display: 'flex', flexDirection: 'column', gap: 6, color: 'var(--text-secondary)' } as const
const note = { marginTop: 6, color: 'var(--text-tertiary)' } as const
const small = { height: 26, fontSize: 'var(--t-small)' } as const
const remove = (
  <Icon name="close" size={11} />
)

/** The new composition (design 1l, the old `comp.isNew` middle): the brief, Plan, and the plan with what rendering it takes, Render and Plan again. */
export function NewComposition({ model }: { readonly model: ComposeViewModel }) {
  useObserved(model)
  const fresh = model.face().fresh
  return (
    <>
      <div>
        <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>New composition</h2>
        <p style={{ margin: '4px 0 0', color: 'var(--text-secondary)' }}>Describe the video. Apogee plans scenes with narration, says what rendering them takes, and renders nothing until you say so.</p>
      </div>
      <label style={{ ...label, fontSize: 'var(--t-small)' }}>
        <span style={{ display: 'flex', justifyContent: 'space-between' }}>
          <span>Brief</span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: fresh.countColor }}>
            {fresh.count}
          </span>
        </span>
        <textarea rows={4} value={fresh.brief} onChange={(event) => model.editDraft({ brief: event.target.value })} placeholder="What the video is, who it’s for, and how it should feel" style={{ boxSizing: 'border-box', width: '100%', resize: 'vertical', padding: '10px 12px', border: '1px solid var(--line-strong)', borderRadius: 8, background: 'var(--surface-card)', fontSize: 'var(--t-prose)', lineHeight: 1.55, color: 'var(--text-primary)' }} />
      </label>
      <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 10 }}>
        {fresh.planning ? (
          <span className="mono" style={{ display: 'inline-flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-ui)', color: 'var(--text-secondary)' }}>
            <span className="pulse" style={{ width: 7, height: 7, borderRadius: '50%', background: 'var(--status-accent)' }} />
            {fresh.planningText}
          </span>
        ) : (
          <button type="button" className="btn-p" onClick={() => model.compose(true)} disabled={fresh.cantPlan}>
            {fresh.planLabel}
          </button>
        )}
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{fresh.planNote}</span>
      </div>
      {fresh.planned ? (
        <div className="card" style={{ padding: '16px 18px' }}>
          <div style={{ display: 'flex', alignItems: 'baseline', flexWrap: 'wrap', gap: '6px 12px' }}>
            <h3 style={{ margin: 0, fontSize: 'var(--t-h3)', fontWeight: 600 }}>{fresh.title}</h3>
            <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
              {fresh.lengthText}
            </span>
          </div>
          <ol style={{ margin: '12px 0 0', padding: 0, listStyle: 'none', display: 'flex', flexDirection: 'column' }}>
            {fresh.scenes.map((scene) => (
              <li key={scene.n} style={{ display: 'grid', gridTemplateColumns: '26px minmax(0, 1fr) auto', gap: '4px 10px', padding: '10px 0', borderTop: '1px solid var(--line-border)' }}>
                <span className="mono" style={{ fontSize: 'var(--t-ui)', color: 'var(--text-tertiary)' }}>
                  {scene.n}.
                </span>
                <div style={{ minWidth: 0 }}>
                  <div style={{ fontSize: 'var(--t-body)' }}>“{scene.narration}”</div>
                  <div style={{ marginTop: 2, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{scene.shows}</div>
                </div>
                <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                  {scene.secs}
                </span>
              </li>
            ))}
          </ol>
          <div style={{ display: 'grid', gridTemplateColumns: '76px minmax(0, 1fr)', gap: '8px 12px', marginTop: 6, paddingTop: 12, borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-small)' }}>
            {fresh.spend.map((line) => (
              <div key={line.label} style={{ display: 'contents' }}>
                <span className="lbl" style={{ paddingTop: 2 }}>
                  {line.label}
                </span>
                <span style={{ color: line.color }}>{line.value}</span>
              </div>
            ))}
          </div>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8, marginTop: 16 }}>
            <button type="button" className="btn-p" onClick={() => model.compose()}>
              Render {fresh.out}
            </button>
            <button type="button" className="btn-g" onClick={() => model.compose(true)}>
              Plan again
            </button>
          </div>
        </div>
      ) : null}
    </>
  )
}

/** The new composition's settings (design 1l, the old `comp.isNew` pane): the planner, the pipeline, the voice and voice model, the bed, the assets, the captions, Save as, the limits and the command. */
export function CompositionSettings({ model }: { readonly model: ComposeViewModel }) {
  useObserved(model)
  const fresh = model.face().fresh
  return (
    <>
      <h2 style={{ margin: 0, fontSize: 'var(--t-h3)', fontWeight: 600 }}>Settings</h2>
      <label style={{ ...label, marginTop: 14 }}>
        Planner
        <select className="inp" value={fresh.planner} onChange={(event) => model.editDraft({ planner: event.target.value })} style={{ width: '100%' }}>
          {fresh.planners.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
      </label>
      <div style={{ marginTop: 6, color: fresh.plannerColor }}>{fresh.plannerNote}</div>
      <label style={{ ...label, marginTop: 12 }}>
        Pipeline
        <select className="inp" value={fresh.pipeline} onChange={(event) => model.editDraft({ model: event.target.value })} style={{ width: '100%' }}>
          {fresh.pipelines.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
      </label>
      <div style={note}>Every clip at 1280×704 and 50 steps, a seed drawn per scene.</div>
      <label style={{ ...label, marginTop: 12 }}>
        Voice
        <select className="inp mono" value={fresh.voice} onChange={(event) => model.editDraft({ voice: event.target.value })} style={{ width: '100%' }}>
          {fresh.voices.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
      </label>
      {fresh.hasVoiceModel ? (
        <label style={{ ...label, marginTop: 12 }}>
          Voice model
          <select className="inp mono" value={fresh.voiceModel} onChange={(event) => model.editDraft({ voiceModel: event.target.value })} style={{ width: '100%' }}>
            {fresh.voiceModels.map((option) => (
              <option key={option.value} value={option.value}>
                {option.label}
              </option>
            ))}
          </select>
        </label>
      ) : null}
      <div style={note}>Every narration is spoken first: its measured length sets its scene’s.</div>
      <div style={{ marginTop: 12 }}>
        <div style={{ marginBottom: 6, color: 'var(--text-secondary)' }}>Music bed</div>
        {fresh.music === '' ? (
          <button type="button" className="btn-g" onClick={() => model.addMusic()} style={small}>
            Add a sound file…
          </button>
        ) : (
          <span className="chip">
            <Icon name="music" size={13} style={{ color: 'var(--text-secondary)' }} />
            <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
              {fresh.music}
            </span>
            <button type="button" className="ib" onClick={() => model.dropMusic()} aria-label="Remove the music bed" style={{ width: 22, height: 22 }}>
              {remove}
            </button>
          </span>
        )}
        <div style={note}>Looped to the video’s length, lowered, and ducked under the narration.</div>
      </div>
      <div style={{ marginTop: 12 }}>
        <div style={{ marginBottom: 6, color: 'var(--text-secondary)' }}>Assets</div>
        {fresh.assets === '' ? (
          <button type="button" className="btn-g" onClick={() => model.addAssets()} style={small}>
            Add a folder…
          </button>
        ) : (
          <span className="chip">
            <Icon name="folder" size={13} style={{ color: 'var(--text-secondary)' }} />
            <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
              {fresh.assets}
            </span>
            <span style={{ color: 'var(--text-tertiary)' }}>{fresh.assetsMeta}</span>
            <button type="button" className="ib" onClick={() => model.dropAssets()} aria-label="Remove the assets folder" style={{ width: 22, height: 22 }}>
              {remove}
            </button>
          </span>
        )}
        <div style={note}>A scene may take one of its clips as it is, or start its clip from one of its stills. The plan can only name what the folder holds.</div>
      </div>
      <label style={{ display: 'flex', alignItems: 'center', gap: 8, marginTop: 14, cursor: 'pointer' }}>
        <input type="checkbox" checked={fresh.captions} onChange={() => model.editDraft({ captions: !fresh.captions })} style={{ accentColor: 'var(--status-accent)' }} />
        Captions
      </label>
      {fresh.captions ? (
        <>
          <div style={{ display: 'flex', alignItems: 'flex-start', gap: 6, marginTop: 6, color: 'var(--text-secondary)' }}>
            <Icon name="captions" size={13} style={{ marginTop: 2 }} />
            <span>{fresh.captionsLine}</span>
          </div>
          <div className="mono" style={{ marginTop: 4, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
            {fresh.captionsRemedy}
          </div>
        </>
      ) : null}
      <label style={{ ...label, marginTop: 14 }}>
        Save as
        <input type="text" className="inp mono" value={fresh.out} onChange={(event) => model.editDraft({ out: event.target.value })} style={{ width: '100%' }} />
      </label>
      <p style={{ margin: '14px 0 0', paddingTop: 12, borderTop: '1px solid var(--line-border)', color: 'var(--text-tertiary)' }}>At most 8 scenes of 2–10 s each, narration up to 240 characters a scene, and the brief up to 2,000.</p>
      <h3 className="lbl" style={{ margin: '14px 0 8px' }}>
        ON THE COMMAND LINE
      </h3>
      <div className="mono" style={{ padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
        {fresh.command}
      </div>
    </>
  )
}
