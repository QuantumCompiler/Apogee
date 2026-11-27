import type { ReactNode } from 'react'
import { InstallCard } from '../../styles/Components/InstallCard'
import { InstallConsentSheet } from '../../styles/Components/InstallConsentSheet'
import { MediaPlayer } from '../../styles/Components/MediaPlayer'
import type { Specimen } from '../../styles/Specimens/Specimen'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VideoViewModel } from '../ViewModels/VideoViewModel'
import { ClipPage, RunCard } from '../Views/ClipPage'
import { ClipsList } from '../Views/ClipsList'
import { GenerateForm } from '../Views/GenerateForm'
import { clipPath, clips } from './VideoWorlds'
import { useVideoPreview } from './VideoPreview'
import type { VideoPreviewOptions } from './VideoPreview'

/**
 * The Video place's specimens for the gallery's Components pages (design
 * 1q): its parts over the video world, as its previews draw them, with no
 * frame or theme of their own -- and `styles`' media pieces it draws (the
 * player, the phase rows, the install card and prompt), each from the face
 * this place's view-model builds over the recordings, as its screen hands it.
 */
function Watched({ model, children }: { readonly model: VideoViewModel; readonly children: ReactNode }) {
  useObserved(model)
  return <div style={{ display: 'flex', flexDirection: 'column', gap: 16, maxWidth: 820 }}>{children}</div>
}

function List() {
  const model = useVideoPreview()
  return (
    <Watched model={model}>
      <ClipsList model={model} />
    </Watched>
  )
}

function Record() {
  const model = useVideoPreview({ then: (video) => video.select(clipPath(clips.final)) })
  return (
    <Watched model={model}>
      <ClipPage model={model} />
    </Watched>
  )
}

const draftFinal = (video: VideoViewModel) => {
  video.act('final', clipPath(clips.draft))
  video.generate()
}

/** The run at `options`' point: the `video:run` preset's 43 ticks, or stopped 12 in, as `RunStopped` stands. */
function Run(options: VideoPreviewOptions) {
  const model = useVideoPreview(options)
  return (
    <Watched model={model}>
      <RunCard model={model} />
    </Watched>
  )
}

function Form() {
  const model = useVideoPreview()
  return (
    <Watched model={model}>
      <GenerateForm model={model} />
    </Watched>
  )
}

/** A clip's player alone, as its page hands it the face: the clip `then` selects, standing where it puts it. */
function Player(options: VideoPreviewOptions) {
  const model = useVideoPreview(options)
  useObserved(model)
  const clip = model.face().clip
  return clip.shown ? (
    <div style={{ width: 440 }}>
      <MediaPlayer face={clip.player} toggle={() => model.togglePlayer()} />
    </div>
  ) : null
}

/** The rain clip made again, refused for its runtime: what asks for the install. */
const refused = (video: VideoViewModel) => {
  video.select(clipPath(clips.rain))
  video.act('again', clipPath(clips.rain))
  video.generate()
}

/** The install's card alone, as the clip list draws it above the clips, `ticks` into the install. */
function Install({ ticks }: { readonly ticks: number }) {
  const model = useVideoPreview({ ticks: 3, then: refused, script: [[(video) => video.install(), ticks]] })
  useObserved(model)
  const install = model.face().install
  return install.shown ? (
    <div style={{ width: 264 }}>
      <InstallCard install={install} act={() => model.installAct()} />
    </div>
  ) : null
}

/** The consent the refusal asks for, its title over the sheet, as the `consent` preset's frame draws it. */
function Consent() {
  const model = useVideoPreview({ ticks: 3, then: refused })
  useObserved(model)
  const consent = model.face().consent
  return consent.shown ? (
    <div style={{ width: 540, display: 'flex', flexDirection: 'column', gap: 12, fontSize: 'var(--t-small)' }}>
      <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{consent.title}</h2>
      <InstallConsentSheet said={consent.said} body={consent.body} command={consent.command} />
    </div>
  ) : null
}

export const specimens: readonly Specimen[] = [
  { name: 'Player · a draft at its start', section: 'media', module: 'video', item: '1k', render: () => <Player then={(video) => video.select(clipPath(clips.draft))} /> },
  { name: 'Player · playing', section: 'media', module: 'video', item: '1k', render: () => <Player ticks={3} then={(video) => (video.select(clipPath(clips.final)), video.togglePlayer())} /> },
  { name: 'Clip rows · the five records, newest first', section: 'media', module: 'video', item: '1k', render: List },
  { name: 'The record · a final made from its draft', section: 'media', module: 'video', item: '1k', render: Record },
  { name: 'What the run says · step 38 of 50', section: 'media', module: 'video', item: '1k', render: () => <Run ticks={43} then={draftFinal} /> },
  { name: 'A run’s progress · stopped', section: 'media', module: 'video', item: '1k', render: () => <Run ticks={12} then={draftFinal} script={[[(video) => video.stop(), 0]]} /> },
  { name: 'The generate form’s controls', section: 'media', module: 'video', item: '1k', render: Form },
  { name: 'Install card · installing', section: 'media', module: 'video', item: '1k', render: () => <Install ticks={4} /> },
  { name: 'Install card · failed, in the CLI’s words', section: 'media', module: 'video', item: '1k', render: () => <Install ticks={12} /> },
  { name: 'The install prompt', section: 'media', module: 'video', item: '1k', render: Consent },
]
