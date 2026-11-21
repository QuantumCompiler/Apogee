import { MediaPlayer } from '../../styles/Components/MediaPlayer'
import { PhaseRow } from '../../styles/Components/PhaseRow'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VideoViewModel } from '../ViewModels/VideoViewModel'
import { Icon } from '../../styles/Components/Icon'

const codeBlock = { padding: '10px 12px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.7, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' } as const

/** The run going now, or the last one until dismissed (design 1k, the old `vid.run`): its file and prompt, its status and Stop, its three phases, what it says and its foot. */
export function RunCard({ model }: { readonly model: VideoViewModel }) {
  useObserved(model)
  const run = model.face().run
  if (!run.shown) return null
  return (
    <>
      <div style={{ display: 'flex', alignItems: 'flex-start', flexWrap: 'wrap', gap: 12 }}>
        <div style={{ flex: '1 1 320px', minWidth: 0 }}>
          <h2 className="mono" style={{ margin: 0, fontSize: 'var(--t-h3)', fontWeight: 600, overflowWrap: 'anywhere' }}>
            {run.file}
          </h2>
          <p className="prose" style={{ margin: '6px 0 0' }}>
            {run.prompt}
          </p>
        </div>
        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, height: 28, fontSize: 'var(--t-small)', color: run.statusColor }}>
          {run.live ? <span className="pulse" style={{ width: 7, height: 7, borderRadius: '50%', background: 'var(--status-accent)' }} /> : null}
          {run.status}
        </span>
        {run.live ? (
          <button type="button" className="btn" onClick={() => void model.stop()}>
            Stop
          </button>
        ) : null}
        {run.ended ? (
          <>
            <button type="button" className="btn-p" onClick={() => model.runAgain()}>
              Run it again
            </button>
            <button type="button" className="btn-g" onClick={() => model.dismissRun()}>
              Dismiss
            </button>
          </>
        ) : null}
      </div>
      <div className="card" style={{ padding: '8px 16px' }}>
        {run.phases.map((phase) => (
          <PhaseRow key={phase.label} phase={phase} />
        ))}
      </div>
      <h3 className="lbl" style={{ margin: '4px 0 -6px' }}>
        WHAT THE RUN SAYS
      </h3>
      <div className="mono" style={codeBlock}>
        {run.log}
      </div>
      <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{run.foot}</p>
    </>
  )
}

/** A clip's page (design 1k, the old `vid.clip`): its name, preset and how it was made, its prompt, the player, its actions, a record from another platform's note, the record's facts and LoRAs, the chain, its files and the command that runs it again. */
export function ClipPage({ model }: { readonly model: VideoViewModel }) {
  useObserved(model)
  const clip = model.face().clip
  if (!clip.shown) return null
  return (
    <>
      <div style={{ minWidth: 0 }}>
        <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 8 }}>
          <h2 className="mono" style={{ margin: 0, fontSize: 'var(--t-h3)', fontWeight: 600, overflowWrap: 'anywhere' }}>
            {clip.file}
          </h2>
          <span className="mono" style={{ padding: '1px 6px', borderRadius: 4, background: 'var(--surface-control)', fontSize: 'var(--t-label)', color: clip.presetColor }}>
            {clip.preset}
          </span>
          <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{clip.modeText}</span>
        </div>
        <p className="prose" style={{ margin: '6px 0 0' }}>
          {clip.prompt}
        </p>
      </div>
      <MediaPlayer face={clip.player} toggle={() => model.togglePlayer()} />
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
        {clip.actions.map((action) => (
          <button key={action.action} type="button" className={action.kind} onClick={() => model.act(action.action, clip.id)} title={action.title}>
            {action.label}
          </button>
        ))}
      </div>
      {clip.note === '' ? null : (
        <div role="note" style={{ display: 'flex', gap: 10, alignItems: 'flex-start', padding: '10px 12px', border: '1px solid var(--line-strong)', borderRadius: 8, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
          <Icon name="error" size={14} style={{ marginTop: 2, color: 'var(--status-warning)' }} />
          <span>{clip.note}</span>
        </div>
      )}
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '22px 32px', marginTop: 4 }}>
        <section aria-label="Record">
          <h3 className="lbl" style={{ margin: '0 0 8px' }}>
            RECORD · {clip.file}.json
          </h3>
          <div style={{ display: 'grid', gridTemplateColumns: '92px minmax(0, 1fr)', gap: '6px 10px', fontSize: 'var(--t-small)' }}>
            {clip.facts.map((fact) => (
              <div key={fact.label} style={{ display: 'contents' }}>
                <span style={{ color: 'var(--text-tertiary)' }}>{fact.label}</span>
                <span className={fact.mono ? 'mono' : undefined} style={{ overflowWrap: 'anywhere', color: 'var(--text-primary)' }}>
                  {fact.value}
                </span>
              </div>
            ))}
          </div>
          {clip.loras.length > 0 ? (
            <>
              <h3 className="lbl" style={{ margin: '18px 0 4px' }}>
                LORAS
              </h3>
              {clip.loras.map((entry) => (
                <div key={entry.path} style={{ padding: '8px 0', borderBottom: '1px solid var(--line-border)', fontSize: 'var(--t-small)' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', gap: 10 }}>
                    <span className="mono" style={{ fontSize: 'var(--t-ui)', overflowWrap: 'anywhere' }}>
                      {entry.path}
                    </span>
                    <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
                      × {entry.scale}
                    </span>
                  </div>
                  <div className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                    sha256 {entry.sha} · adapter {entry.adapter}
                  </div>
                </div>
              ))}
            </>
          ) : null}
        </section>
        <section aria-label="Chain">
          <h3 className="lbl" style={{ margin: '0 0 10px' }}>
            CHAIN
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column' }}>
            {clip.chain.map((link, index) => (
              <div key={link.label} style={{ display: 'grid', gridTemplateColumns: '14px minmax(0, 1fr)', columnGap: 10 }}>
                <span aria-hidden="true" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
                  <span style={{ width: 9, height: 9, marginTop: 4, boxSizing: 'border-box', borderRadius: '50%', border: `1.5px solid ${index === 0 ? 'var(--status-accent)' : 'var(--text-tertiary)'}`, background: index === 0 ? 'var(--status-accent)' : 'transparent' }} />
                  <span style={{ flex: '1 1 auto', width: 1, minHeight: 14, background: index === clip.chain.length - 1 ? 'transparent' : 'var(--line-strong)' }} />
                </span>
                <div style={{ paddingBottom: 12, minWidth: 0 }}>
                  {link.go === '' ? (
                    <span className="mono" style={{ fontSize: 'var(--t-ui)', color: 'var(--text-primary)' }}>
                      {link.label}
                    </span>
                  ) : (
                    <button type="button" onClick={() => model.select(link.go)} className="mono" style={{ padding: 0, border: 0, background: 'transparent', fontSize: 'var(--t-ui)', color: 'var(--status-accent)', textAlign: 'left', textDecoration: 'underline', textDecorationColor: 'var(--line-strong)', textUnderlineOffset: 3 }}>
                      {link.label}
                    </button>
                  )}
                  <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{link.how}</div>
                </div>
              </div>
            ))}
          </div>
          <h3 className="lbl" style={{ margin: '8px 0 8px' }}>
            FILES · ~/Movies/Apogee
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            {clip.files.map((file) => (
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
          <h3 className="lbl" style={{ margin: '18px 0 8px' }}>
            THIS RUN AGAIN, FROM THE COMMAND LINE
          </h3>
          <div className="mono" style={{ padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
            {clip.command}
          </div>
        </section>
      </div>
    </>
  )
}
