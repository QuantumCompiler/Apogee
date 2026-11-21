import modelsListKinds from '../../../../fixtures/business/reads/models-list-kinds-handmade.json?raw'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VoiceViewModel } from '../ViewModels/VoiceViewModel'
import { VoiceScreen } from '../Views/VoiceScreen'
import { secondSentence, speeches, voiceFoldersWithUntimedRecord, voiceFoldersWithoutSpeech } from './VoiceWorlds'
import { useVoicePreview } from './VoicePreview'
import type { VoicePreviewOptions } from './VoicePreview'

/** The Voice place on its own (design 1m), one preview per state a person sees differently, over the Voice world: the Mac's names where they fit. */
const frame = { position: 'relative', width: 1180, height: 900, display: 'flex', flexDirection: 'column', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Frame({ model }: { readonly model: VoiceViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <VoiceScreen model={model} />
    </div>
  )
}

function Place(options: VoicePreviewOptions) {
  return <Frame model={useVoicePreview(options)} />
}

const torch = (voice: VoiceViewModel) => voice.select(speeches.torch)

/** A speech selected, its second sentence lit: speech-torch.json played from its 0.05 s, as a row pressed plays it. */
export const SpeechSelected = () => <Place then={(voice) => (torch(voice), voice.seek(1))} />

/** The `voice` preset's frame: speech-torch.json paused 0.06 s in, on its second sentence, the waveform drawn that far. */
export const PausedOnSecondSentence = () => <Place then={(voice) => (torch(voice), voice.hold(secondSentence))} />

/** The MLX speech: its model named in its command, its device and versions MLX's. */
export const MlxSpeech = () => <Place then={(voice) => voice.select(speeches.mlx)} />

/** No speeches yet: an empty folder, "Nothing spoken yet." */
export const NothingSpokenYet = () => <Place folders={{}} />

/** Speaking: the sample, three ticks into the run, its row pulsing and its line on the cpu the run said. */
export const Speaking = () => <Place then={(voice) => voice.speak()} ticks={3} />

/** Spoken: the run ended, its record read back from the folder and shown. */
export const Spoken = () => <Place then={(voice) => voice.speak()} ticks={6} />

/** The card refusing text over 4,000 characters: the count in the error color, Speak off. */
export const TextTooLong = () => <Place then={(voice) => voice.setText('Apogee runs every model on this Mac. '.repeat(110))} />

/** The card refusing a name that exists: a speech never writes over a file. */
export const NameExists = () => <Place then={(voice) => voice.setOut('hello-pytorch.wav')} />

/** A speech nobody recorded: the stand-in's words under Speak, in the error color. */
export const SpeechFailed = () => <Place then={(voice) => (voice.setText('Something nobody recorded.'), voice.speak())} ticks={2} />

/** New speech: the card empty from the chosen voice, no speech shown. */
export const NewSpeech = () => <Place voice="bf_emma" then={(voice) => voice.newSpeech()} />

/** A record without its segments beside the two, passed over and said. */
export const RecordWithoutSegments = () => <Place folders={voiceFoldersWithUntimedRecord()} />

/** A record whose WAV is gone: passed over, and said why. */
export const RecordWhoseFileIsGone = () => <Place folders={voiceFoldersWithoutSpeech('hello-mlx.wav')} />

/** A store with no voice model: the header and the card say so in the CLI's words. */
export const NoVoiceModel = () => <Place rules={{ reads: [{ arguments: ['models', 'list'], answer: { stdout: modelsListKinds } }] }} />

/** With no apogee: the speeches, which are files, and nothing spoken. */
export const Missing = () => <Place apogee="missing" />
