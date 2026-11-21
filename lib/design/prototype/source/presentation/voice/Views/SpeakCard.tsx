import { useObserved } from '../../styles/Observation/useObserved'
import type { VoiceViewModel } from '../ViewModels/VoiceViewModel'

/**
 * The speak card (design 1m, the old `section aria-label="Speak"`): the text
 * and its count, the sentence line, the model's voices by language, the speed
 * and its reset, Save as, what is wrong, the command, Speak and what it
 * speaks on -- or, speaking, the run's line.
 */
export function SpeakCard({ model }: { readonly model: VoiceViewModel }) {
  useObserved(model)
  const card = model.face().card
  return (
    <section className="card" aria-label="Speak" style={{ padding: '16px 18px', display: 'flex', flexDirection: 'column', gap: 14 }}>
      <label style={{ display: 'flex', flexDirection: 'column', gap: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
        <span style={{ display: 'flex', justifyContent: 'space-between' }}>
          <span>Text</span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: card.countColor }}>
            {card.count}
          </span>
        </span>
        <textarea rows={5} value={card.text} onChange={(event) => model.setText(event.target.value)} placeholder="What to say" style={{ boxSizing: 'border-box', width: '100%', resize: 'vertical', padding: '10px 12px', border: '1px solid var(--line-strong)', borderRadius: 8, background: 'var(--surface-window)', fontSize: 'var(--t-prose)', lineHeight: 1.55, color: 'var(--text-primary)' }} />
      </label>
      <div className="mono" style={{ marginTop: -6, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
        {card.sentencesText}
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
        {card.groups.map((group) => (
          <div key={group.title}>
            <div className="lbl" style={{ marginBottom: 6 }}>
              {group.title}
            </div>
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
              {group.voices.map((voice) => (
                <button key={voice.id} type="button" aria-pressed={voice.current} onClick={() => model.pickVoice(voice.id)} title={voice.title} className="mono" style={{ display: 'inline-flex', alignItems: 'center', gap: 6, height: 28, padding: '0 10px', border: `1px solid ${voice.current ? 'var(--status-accent)' : 'var(--line-border)'}`, borderRadius: 6, background: voice.current ? 'var(--surface-selection)' : 'transparent', color: 'var(--text-primary)', fontSize: 'var(--t-ui)' }}>
                  {voice.id}
                  {voice.hint === '' ? null : <span style={{ color: 'var(--text-tertiary)' }}>{voice.hint}</span>}
                </button>
              ))}
            </div>
          </div>
        ))}
      </div>
      <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: '10px 14px', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
        <label style={{ display: 'inline-flex', alignItems: 'center', gap: 10 }}>
          Speed
          <input type="range" min="0.5" max="2" step="0.05" value={card.speed} onChange={(event) => model.setSpeed(Number(event.target.value))} aria-label="Speed" style={{ width: 180, accentColor: 'var(--status-accent)' }} />
        </label>
        <span className="mono" style={{ minWidth: 44, fontSize: 'var(--t-ui)', color: 'var(--text-primary)' }}>
          {card.speedText}
        </span>
        {card.speedChanged ? (
          <button type="button" className="btn-g" onClick={() => model.resetSpeed()} style={{ height: 24, fontSize: 'var(--t-small)' }}>
            1.0×
          </button>
        ) : null}
        <span style={{ flex: '1 1 auto' }} />
        <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
          Save as
          <input type="text" className="inp mono" value={card.out} onChange={(event) => model.setOut(event.target.value)} style={{ width: 220 }} />
        </label>
      </div>
      {card.errors.length === 0 ? null : (
        <div role="alert" style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: '10px 12px', border: '1px solid var(--status-error)', borderRadius: 6, fontSize: 'var(--t-small)' }}>
          {card.errors.map((error) => (
            <span key={error}>{error}</span>
          ))}
        </div>
      )}
      <div className="mono" style={{ padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
        {card.command}
      </div>
      <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: 10 }}>
        <button type="button" className="btn-p" onClick={() => model.speak()} disabled={card.cantSpeak} style={{ height: 32, padding: '0 16px' }}>
          Speak
        </button>
        {card.speaking ? (
          <span className="mono" style={{ display: 'inline-flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-ui)', color: 'var(--text-secondary)' }}>
            <span className="pulse" style={{ width: 7, height: 7, borderRadius: '50%', background: 'var(--status-accent)' }} />
            {card.speakingText}
          </span>
        ) : null}
        <span style={{ fontSize: 'var(--t-small)', color: card.noteColor }}>{card.note}</span>
      </div>
    </section>
  )
}
