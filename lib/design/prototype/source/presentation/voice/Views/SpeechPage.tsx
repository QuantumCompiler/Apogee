import { PlayToggle } from '../../styles/Components/PlayToggle'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VoiceViewModel } from '../ViewModels/VoiceViewModel'

const table = { display: 'grid', gridTemplateColumns: '28px minmax(0, 1fr) 64px 64px', gap: 10 } as const

/**
 * The selected speech (design 1m, the old `vox.hasSel` section): its file and
 * meta with the three actions, the waveform over the one player (`styles`'
 * `PlayToggle` on the shared `Playback`), the sentence table with STARTS and
 * LASTS from the record's own timings -- a row plays from its start -- what
 * was measured, the record's facts and the command that says it again.
 */
export function SpeechPage({ model }: { readonly model: VoiceViewModel }) {
  useObserved(model)
  const speech = model.face().speech
  return (
    <section aria-label="Speech" style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
      <div style={{ display: 'flex', alignItems: 'flex-start', flexWrap: 'wrap', gap: 12 }}>
        <div style={{ flex: '1 1 300px', minWidth: 0 }}>
          <h2 className="mono" style={{ margin: 0, fontSize: 'var(--t-h3)', fontWeight: 600, overflowWrap: 'anywhere' }}>
            {speech.file}
          </h2>
          <p className="mono" style={{ margin: '6px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            {speech.meta}
          </p>
        </div>
        {speech.actions.map((action) => (
          <button key={action.action} type="button" className={action.kind} onClick={() => model.act(action.action)}>
            {action.label}
          </button>
        ))}
      </div>
      <div className="card" style={{ padding: '14px 16px' }}>
        <div aria-hidden="true" style={{ display: 'flex', alignItems: 'center', gap: 2, height: 64 }}>
          {speech.bars.map((bar, index) => (
            <span key={index} style={{ flex: '1 1 0', height: `${bar.height}%`, minHeight: 2, borderRadius: 1, background: bar.color }} />
          ))}
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginTop: 10 }}>
          <PlayToggle face={speech.player} toggle={() => model.toggle()} />
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>
            {speech.player.time}
          </span>
          <span style={{ flex: '1 1 auto' }} />
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            {speech.format}
          </span>
        </div>
      </div>
      <div>
        <div className="lbl" style={{ ...table, paddingBottom: 6, borderBottom: '1px solid var(--line-strong)' }}>
          <span>#</span>
          <span>SENTENCE</span>
          <span style={{ textAlign: 'right' }}>STARTS</span>
          <span style={{ textAlign: 'right' }}>LASTS</span>
        </div>
        {speech.sentences.map((row, index) => (
          <button key={row.n} type="button" className="hv" onClick={() => model.seek(index)} aria-label={`Play from sentence ${row.n}`} style={{ ...table, width: '100%', alignItems: 'baseline', padding: '8px 0', border: 0, borderBottom: '1px solid var(--line-border)', borderRadius: 0, background: row.lit ? 'var(--surface-selection)' : 'transparent', color: 'var(--text-primary)', textAlign: 'left', fontSize: 'var(--t-body)' }}>
            <span className="mono" style={{ fontSize: 'var(--t-ui)', color: 'var(--text-tertiary)' }}>
              {row.n}
            </span>
            <span>{row.text}</span>
            <span className="mono" style={{ fontSize: 'var(--t-ui)', textAlign: 'right', color: 'var(--text-secondary)' }}>
              {row.starts}
            </span>
            <span className="mono" style={{ fontSize: 'var(--t-ui)', textAlign: 'right', color: 'var(--text-secondary)' }}>
              {row.lasts}
            </span>
          </button>
        ))}
        <p style={{ margin: '8px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{speech.measured}</p>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(260px, 1fr))', gap: '18px 32px' }}>
        <div>
          <h3 className="lbl" style={{ margin: '0 0 8px' }}>
            RECORD · {speech.record}
          </h3>
          <div style={{ display: 'grid', gridTemplateColumns: '82px minmax(0, 1fr)', gap: '6px 10px', fontSize: 'var(--t-small)' }}>
            {speech.facts.map((fact) => (
              <div key={fact.label} style={{ display: 'contents' }}>
                <span style={{ color: 'var(--text-tertiary)' }}>{fact.label}</span>
                <span style={{ overflowWrap: 'anywhere', fontFamily: fact.mono ? 'var(--font-mono)' : 'inherit' }}>{fact.value}</span>
              </div>
            ))}
          </div>
        </div>
        <div>
          <h3 className="lbl" style={{ margin: '0 0 8px' }}>
            THIS SPEECH, FROM THE COMMAND LINE
          </h3>
          <div className="mono" style={{ padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
            {speech.command}
          </div>
        </div>
      </div>
    </section>
  )
}
