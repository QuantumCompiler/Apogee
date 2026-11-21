import { useObserved } from '../../styles/Observation/useObserved'
import type { VoiceViewModel } from '../ViewModels/VoiceViewModel'
import { SpeakCard } from './SpeakCard'
import { SpeechList } from './SpeechList'
import { SpeechPage } from './SpeechPage'

/**
 * The Voice place (design 1m): the old prototype's screen (`main.html` at
 * `b375fac9`, `ui.isVoice`) -- its header with the store's voice model and
 * New speech, then the speeches list beside the speak card and the selected
 * speech. The speak card is inline, as the prototype draws it; the Mac's say
 * sheet stays Mac-only.
 */
export function VoiceScreen({ model }: { readonly model: VoiceViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 14, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Voice</h1>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Text to speech, on this Mac</span>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="hv" onClick={() => model.openModel()} disabled={!face.canOpenModel} title={face.modelTitle} style={{ display: 'inline-flex', alignItems: 'center', gap: 7, height: 28, padding: '0 8px', border: 0, borderRadius: 6, background: 'transparent', color: 'var(--text-tertiary)', fontSize: 'var(--t-small)', whiteSpace: 'nowrap' }}>
          <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--model-local)' }} />
          {face.modelLine}
        </button>
        <button type="button" className="btn-p" onClick={() => model.newSpeech()}>
          <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
            <path d="M8 3.5v9M3.5 8h9" />
          </svg>
          New speech
        </button>
      </header>
      <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
        <SpeechList model={model} />
        <div style={{ flex: '1 1 0', minWidth: 0, minHeight: 0, overflowY: 'auto', padding: '20px 28px 32px' }}>
          <div style={{ maxWidth: 820, display: 'flex', flexDirection: 'column', gap: 18 }}>
            <SpeakCard model={model} />
            {face.speech.shown ? <SpeechPage model={model} /> : null}
          </div>
        </div>
      </div>
    </div>
  )
}
