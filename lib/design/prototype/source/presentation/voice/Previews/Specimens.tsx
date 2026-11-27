import type { ReactNode } from 'react'
import { PlayToggle } from '../../styles/Components/PlayToggle'
import type { Specimen } from '../../styles/Specimens/Specimen'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VoiceViewModel } from '../ViewModels/VoiceViewModel'
import { SpeakCard } from '../Views/SpeakCard'
import { SpeechList } from '../Views/SpeechList'
import { SpeechPage } from '../Views/SpeechPage'
import { secondSentence, speeches } from './VoiceWorlds'
import { useVoicePreview } from './VoicePreview'

/**
 * The Voice place's specimens for the gallery's Components pages (design
 * 1q): its parts over the Voice world, as its previews draw them, with no
 * frame or theme of their own -- and `styles`' play button, from the face
 * this place's view-model builds for the selected speech.
 */
function Watched({ model, children }: { readonly model: VoiceViewModel; readonly children: ReactNode }) {
  useObserved(model)
  return <div style={{ display: 'flex', flexDirection: 'column', gap: 16, maxWidth: 820 }}>{children}</div>
}

function List() {
  const model = useVoicePreview()
  return (
    <Watched model={model}>
      <SpeechList model={model} />
    </Watched>
  )
}

function Voices() {
  const model = useVoicePreview()
  return (
    <Watched model={model}>
      <SpeakCard model={model} />
    </Watched>
  )
}

function Speech() {
  const model = useVoicePreview({ then: (voice) => (voice.select(speeches.torch), voice.hold(secondSentence)) })
  return (
    <Watched model={model}>
      <SpeechPage model={model} />
    </Watched>
  )
}

/** The player's button alone, as the speech page hands it the face: paused on the speech's second sentence, as the `voice` preset stands. */
function Toggle() {
  const model = useVoicePreview({ then: (voice) => (voice.select(speeches.torch), voice.hold(secondSentence)) })
  useObserved(model)
  const speech = model.face().speech
  return <PlayToggle face={speech.player} toggle={() => model.toggle()} />
}

export const specimens: readonly Specimen[] = [
  { name: 'Speeches · the two recorded, newest first', section: 'media', module: 'voice', item: '1m', render: List },
  { name: 'Voices · the speak card', section: 'media', module: 'voice', item: '1m', render: Voices },
  { name: 'A speech · its waveform and sentences', section: 'media', module: 'voice', item: '1m', render: Speech },
  { name: 'Play button · paused', section: 'media', module: 'voice', item: '1m', render: Toggle },
]
