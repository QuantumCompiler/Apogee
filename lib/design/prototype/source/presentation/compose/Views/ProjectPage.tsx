import { MediaPlayer } from '../../styles/Components/MediaPlayer'
import { PhaseRow } from '../../styles/Components/PhaseRow'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'

const mono = { fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' } as const

/**
 * A composition's page (design 1l, the old `comp.isProject` middle): its
 * title, status and Stop or Resume, the brief, the render's phases and the
 * stop note, an edit's changes, the player, the timeline, the scenes with
 * Re-roll, its files and settings, and its chain.
 */
export function ProjectPage({ model }: { readonly model: ComposeViewModel }) {
  useObserved(model)
  const p = model.face().page
  const project = model.selectedProject
  return (
    <>
      <div style={{ display: 'flex', alignItems: 'flex-start', flexWrap: 'wrap', gap: 12 }}>
        <div style={{ flex: '1 1 320px', minWidth: 0 }}>
          <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8 }}>
            <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{p.title}</h2>
            {p.tag === '' ? null : (
              <span className="mono" style={{ padding: '1px 6px', borderRadius: 4, background: 'var(--surface-control)', fontSize: 'var(--t-label)', color: 'var(--text-secondary)' }}>
                {p.tag}
              </span>
            )}
          </div>
          <p className="mono" style={{ margin: '6px 0 0', ...mono }}>
            {p.meta}
          </p>
        </div>
        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, height: 28, fontSize: 'var(--t-small)', color: p.statusColor }}>
          {p.rendering ? <span className="pulse" style={{ width: 7, height: 7, borderRadius: '50%', background: 'var(--status-accent)' }} /> : null}
          {p.status}
        </span>
        {p.rendering ? (
          <button type="button" className="btn" onClick={() => void model.stop()}>
            Stop
          </button>
        ) : null}
        {p.stopped && project ? (
          <button type="button" className="btn-p" onClick={() => model.resume(project)}>
            Resume
          </button>
        ) : null}
      </div>
      <p className="prose" style={{ margin: '-4px 0 0', color: 'var(--text-secondary)' }}>
        {p.brief}
      </p>
      {p.showProgress ? (
        <>
          <div className="card" style={{ padding: '8px 16px' }}>
            {p.phases.map((phase) => (
              <PhaseRow key={phase.label} phase={phase} />
            ))}
          </div>
          {p.stopNote === '' ? null : (
            <div role="note" style={{ display: 'flex', gap: 10, alignItems: 'flex-start', padding: '10px 12px', border: '1px solid var(--line-strong)', borderRadius: 8, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
              <span aria-hidden="true" style={{ flex: '0 0 auto', width: 8, height: 8, marginTop: 5, borderRadius: 2, background: 'var(--status-warning)' }} />
              <span>{p.stopNote}</span>
            </div>
          )}
        </>
      ) : null}
      {p.isEdit ? (
        <section aria-label="Changes">
          {p.differs === '' ? null : (
            <p style={{ display: 'flex', alignItems: 'flex-start', gap: 6, margin: '0 0 10px', fontSize: 'var(--t-small)', color: 'var(--status-warning)' }}>
              <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true" style={{ flex: '0 0 auto', marginTop: 2 }}>
                <path d="M8 2.5l6 10.5H2z" />
                <path d="M8 6.5v3M8 11.2v.1" />
              </svg>
              <span>{p.differs}</span>
            </p>
          )}
          <h3 className="lbl" style={{ margin: '0 0 6px' }}>
            CHANGES
          </h3>
          <div style={{ display: 'grid', gridTemplateColumns: '22px auto minmax(0, 1fr)', gap: '3px 12px', alignItems: 'baseline' }}>
            {p.changes.map((change) => (
              <div key={`${change.n}-${change.text}`} style={{ display: 'contents' }}>
                <span className="mono" style={mono}>
                  {change.n}
                </span>
                <span style={{ fontSize: 'var(--t-small)', color: change.color }}>{change.text}</span>
                <span style={{ minWidth: 0, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{change.narration}</span>
              </div>
            ))}
          </div>
        </section>
      ) : null}
      {p.showPlayer ? <MediaPlayer face={p.player} toggle={() => model.toggle()} /> : null}
      <Timeline model={model} />
      <section aria-label="Scenes">
        <h3 className="lbl" style={{ margin: '8px 0 4px' }}>
          SCENES
        </h3>
        {p.scenes.map((scene, index) => (
          <div key={scene.n} style={{ display: 'grid', gridTemplateColumns: '26px minmax(0, 1fr) auto', gap: '4px 12px', padding: '12px 10px', margin: '0 -10px', borderTop: '1px solid var(--line-border)', borderRadius: 6, background: scene.playing ? 'var(--surface-selection)' : 'transparent' }}>
            <button type="button" onClick={() => model.seek(index)} className="mono" aria-label={`Play scene ${scene.n}`} style={{ alignSelf: 'start', padding: 0, border: 0, background: 'transparent', fontSize: 'var(--t-ui)', color: 'var(--text-tertiary)', textAlign: 'left' }}>
              {scene.n}.
            </button>
            <div style={{ minWidth: 0 }}>
              <div style={{ fontSize: 'var(--t-body)' }}>“{scene.narration}”</div>
              <div style={{ marginTop: 3, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{scene.shows}</div>
              <div className="mono" style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 14px', marginTop: 6, ...mono }}>
                <span style={{ color: scene.voiceColor }}>{scene.voiceText}</span>
                <span style={{ color: scene.clipColor }}>{scene.clipText}</span>
                <span>{scene.seedText}</span>
              </div>
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-end', gap: 6 }}>
              <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
                {scene.secs}
              </span>
              {scene.canRedo ? (
                <label style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)', cursor: 'pointer' }}>
                  <input type="checkbox" checked={scene.redo} onChange={() => model.toggleRedo(Number(scene.n))} style={{ accentColor: 'var(--status-accent)' }} />
                  Re-roll
                </label>
              ) : null}
            </div>
          </div>
        ))}
      </section>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '22px 32px' }}>
        <section aria-label="Files">
          <h3 className="lbl" style={{ margin: '0 0 8px' }}>
            FILES · {p.folder}
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            {p.files.map((file) => (
              <div key={file.path} style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: 10 }}>
                <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
                  {file.path}
                </span>
                <span className="mono" style={mono}>
                  {file.what}
                </span>
              </div>
            ))}
          </div>
          <h3 className="lbl" style={{ margin: '18px 0 8px' }}>
            SETTINGS
          </h3>
          <div style={{ display: 'grid', gridTemplateColumns: '82px minmax(0, 1fr)', gap: '6px 10px', fontSize: 'var(--t-small)' }}>
            {p.facts.map((fact) => (
              <div key={fact.label} style={{ display: 'contents' }}>
                <span style={{ color: 'var(--text-tertiary)' }}>{fact.label}</span>
                <span style={{ overflowWrap: 'anywhere', fontFamily: fact.mono ? 'var(--font-mono)' : 'inherit', color: fact.color }}>{fact.value}</span>
              </div>
            ))}
          </div>
        </section>
        <section aria-label="Chain">
          <h3 className="lbl" style={{ margin: '0 0 10px' }}>
            CHAIN
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column' }}>
            {p.chain.map((link) => (
              <div key={link.output} style={{ display: 'grid', gridTemplateColumns: '14px minmax(0, 1fr)', columnGap: 10 }}>
                <span aria-hidden="true" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
                  <span style={{ width: 9, height: 9, marginTop: 4, boxSizing: 'border-box', borderRadius: '50%', border: `1.5px solid ${link.first ? 'var(--status-accent)' : 'var(--text-tertiary)'}`, background: link.first ? 'var(--status-accent)' : 'transparent' }} />
                  <span style={{ flex: '1 1 auto', width: 1, minHeight: 14, background: link.last ? 'transparent' : 'var(--line-strong)' }} />
                </span>
                <div style={{ paddingBottom: 12, minWidth: 0 }}>
                  {link.link ? (
                    <button type="button" onClick={() => model.select(link.output)} className="mono" style={{ padding: 0, border: 0, background: 'transparent', fontSize: 'var(--t-ui)', color: 'var(--status-accent)', textAlign: 'left', textDecoration: 'underline', textDecorationColor: 'var(--line-strong)', textUnderlineOffset: 3 }}>
                      {link.label}
                    </button>
                  ) : (
                    <span className="mono" style={{ fontSize: 'var(--t-ui)', color: 'var(--text-primary)' }}>
                      {link.label}
                    </span>
                  )}
                  <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{link.how}</div>
                </div>
              </div>
            ))}
          </div>
        </section>
      </div>
    </>
  )
}

/** A composition's timeline (design 1l, the old `comp.p.tl`): the picture's scenes, each a door to its start, the narration, the caption cues and the bed, with the playhead. */
export function Timeline({ model }: { readonly model: ComposeViewModel }) {
  useObserved(model)
  const tl = model.face().page.timeline
  const track = { position: 'relative', height: 14 } as const
  return (
    <section aria-label="Timeline">
      <h3 className="lbl" style={{ margin: '4px 0 10px', display: 'flex', justifyContent: 'space-between', gap: 12 }}>
        TIMELINE<span style={{ letterSpacing: 0, fontWeight: 400 }}>{tl.note}</span>
      </h3>
      <div style={{ display: 'grid', gridTemplateColumns: '72px minmax(0, 1fr)', gap: '6px 10px', alignItems: 'center' }}>
        <span className="mono" style={mono}>
          picture
        </span>
        <div style={{ position: 'relative', height: 34 }}>
          {tl.pictures.map((block, index) => (
            <button key={block.n} type="button" onClick={() => model.seek(index)} aria-label={`Scene ${block.n}, from ${block.at}`} aria-pressed={block.current} title={`Scene ${block.n} · ${block.at} · ${block.len}`} style={{ position: 'absolute', left: `${block.left}%`, width: `${block.width}%`, top: 0, bottom: 0, boxSizing: 'border-box', padding: '0 6px', border: `1px solid ${block.current ? 'var(--status-accent)' : 'var(--line-strong)'}`, borderRadius: 4, background: block.made ? 'var(--surface-control)' : 'transparent', color: block.made ? 'var(--text-primary)' : 'var(--text-tertiary)', textAlign: 'left', fontFamily: 'var(--font-mono)', fontSize: 'var(--t-meta)', overflow: 'hidden', whiteSpace: 'nowrap' }}>
              {block.n}
            </button>
          ))}
          <span aria-hidden="true" style={{ position: 'absolute', left: `${tl.head}%`, top: -4, bottom: -4, width: 2, borderRadius: 1, background: 'var(--status-accent)' }} />
        </div>
        <span className="mono" style={mono}>
          narration
        </span>
        <div style={track}>
          {tl.narration.map((bar, index) => (
            <span key={index} style={{ position: 'absolute', left: `${bar.left}%`, width: `${bar.width}%`, top: 2, bottom: 2, borderRadius: 3, background: bar.made ? 'var(--text-tertiary)' : 'var(--line-border)' }} />
          ))}
        </div>
        <span className="mono" style={mono}>
          captions
        </span>
        <div style={track}>
          {tl.cues.map((cue, index) => (
            <span key={index} style={{ position: 'absolute', left: `${cue.left}%`, width: `${cue.width}%`, top: 4, bottom: 4, boxSizing: 'border-box', border: '1px solid var(--text-tertiary)', borderRadius: 2 }} />
          ))}
        </div>
        <span className="mono" style={mono}>
          music
        </span>
        <div style={track}>
          {tl.bed ? (
            <>
              <span style={{ position: 'absolute', left: 0, right: 0, top: 5, height: 4, borderRadius: 2, background: 'var(--line-strong)' }} />
              {tl.narration.map((bar, index) => (
                <span key={index} style={{ position: 'absolute', left: `${bar.left}%`, width: `${bar.width}%`, top: 5, height: 4, background: 'var(--line-border)' }} />
              ))}
            </>
          ) : (
            <span className="mono" style={{ position: 'absolute', left: 0, top: -1, fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
              none
            </span>
          )}
        </div>
        <span />
        <div className="mono" style={{ display: 'flex', justifyContent: 'space-between', fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
          <span>0:00</span>
          <span>{tl.legend}</span>
          <span>{tl.end}</span>
        </div>
      </div>
    </section>
  )
}
