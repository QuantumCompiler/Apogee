import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VoiceViewModel } from '../ViewModels/VoiceViewModel'
import { SpeakCard } from '../Views/SpeakCard'
import { SpeechList } from '../Views/SpeechList'
import { SpeechPage } from '../Views/SpeechPage'
import { secondSentence, speeches } from './VoiceWorlds'
import { useVoicePreview } from './VoicePreview'

/** Each part of the Voice place on its own (design 1m), over the Voice world, as the screen draws it. */
const frame = { display: 'flex', width: 'max-content', maxWidth: 1180, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Part({ model, children }: { readonly model: VoiceViewModel; readonly children: ReactNode }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {children}
    </div>
  )
}

/** The speeches list alone: the two recorded speeches, the newest first. */
export const List = () => {
  const model = useVoicePreview()
  return (
    <Part model={model}>
      <SpeechList model={model} />
    </Part>
  )
}

/** The speak card alone: the old frame's sample, the model's three voices in two languages. */
export const Card = () => {
  const model = useVoicePreview()
  return (
    <Part model={model}>
      <div style={{ width: 820, padding: 20 }}>
        <SpeakCard model={model} />
      </div>
    </Part>
  )
}

/** The selected speech alone: speech-torch.json, paused on its second sentence. */
export const Page = () => {
  const model = useVoicePreview({ then: (voice) => (voice.select(speeches.torch), voice.hold(secondSentence)) })
  return (
    <Part model={model}>
      <div style={{ width: 820, padding: 20 }}>
        <SpeechPage model={model} />
      </div>
    </Part>
  )
}
