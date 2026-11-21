import type { ReactNode } from 'react'
import { InstallConsentSheet } from '../../styles/Components/InstallConsentSheet'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VideoViewModel } from '../ViewModels/VideoViewModel'
import { CompositionsPlaceholder, VideoScreen } from '../Views/VideoScreen'
import { clipPath, clips, videoFoldersWithoutClip } from './VideoWorlds'
import { useVideoPreview } from './VideoPreview'
import type { VideoPreviewOptions } from './VideoPreview'

/** The Video place on its own (design 1k), one preview per state a person sees differently, over the video world: the Mac's 27 names where they fit. */
const frame = { position: 'relative', width: 1180, height: 820, display: 'flex', flexDirection: 'column', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Frame({ model, children }: { readonly model: VideoViewModel; readonly children?: ReactNode }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <VideoScreen model={model} compositions={() => <CompositionsPlaceholder />} />
      {children}
    </div>
  )
}

function Place(options: VideoPreviewOptions) {
  return <Frame model={useVideoPreview(options)} />
}

const rain = (video: VideoViewModel) => {
  video.select(clipPath(clips.rain))
  video.act('again', clipPath(clips.rain))
}
const refused = (video: VideoViewModel) => {
  rain(video)
  video.generate()
}
const draftFinal = (video: VideoViewModel) => {
  video.act('final', clipPath(clips.draft))
  video.generate()
}

/** Clips: a final, made from its draft -- the `video` preset's frame: its page, the player paused 2.1 s in, the paper boat's draft on the right. */
export const ClipsFinalFromDraft = () => <Place then={(video) => (video.select(clipPath(clips.final)), video.playback.hold(`clip:${clipPath(clips.final)}`, 2.1))} />

/** No clips yet: an empty folder, how a clip is made said. */
export const NoClipsYet = () => <Place folders={{}} />

/** Clips beside a record whose clip is gone: passed over, and said why. */
export const RecordWhoseClipIsGone = () => <Place folders={videoFoldersWithoutClip(clips.harbor)} />

/** A draft, its final not made: Make the final offered first. */
export const DraftFinalNotMade = () => <Place then={(video) => video.select(clipPath(clips.draft))} />

/** A clip made elsewhere, with a LoRA: its note, its LoRA and its hash, the form run again from its record. */
export const ClipWithLoras = () => <Place then={rain} />

/** Playing: the player 1.2 s along, Pause, the frame it is on. */
export const Playing = () => <Place ticks={3} then={(video) => (video.select(clipPath(clips.final)), video.togglePlayer())} />

/** A run: step 38 of 50 -- the `video:run` preset's frame: the draft made final, denoising, what it says so far. */
export const Generating = () => <Place ticks={43} then={draftFinal} />

/** A run stopped by you: nothing written, Run it again and Dismiss. */
export const RunStopped = () => <Place ticks={12} then={draftFinal} script={[[(video) => video.stop(), 0]]} />

/** A run made: its clip landed first in the list and shown, its record beside it. */
export const RunMade = () => <Place ticks={70} then={draftFinal} />

/** A run failed: a command nobody recorded, which the stand-in says in its words. */
export const RunFailed = () => <Place ticks={2} then={(video) => video.generate()} />

/** New clip: the form fresh, the default pipeline, a draft. */
export const NewClip = () => <Place then={(video) => video.startFresh()} />

/** Run again, from a record: what the CLI will take from it, the seed held. */
export const RunAgainFromRecord = () => <Place then={(video) => video.act('again', clipPath(clips.final))} />

/** Continue the clip: its last frame, its frame and rate locked. */
export const ContinueTheClip = () => <Place then={(video) => video.act('extend', clipPath(clips.final))} />

/** New clip: the pipeline beside its MLX conversion picked, so the engine is said: --engine torch. */
export const PipelinePickedEngineTorch = () => <Place then={(video) => video.edit({ pipeline: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6' })} />

/** The form refused as typed: its errors, Generate off. */
export const FormErrors = () => <Place then={(video) => video.edit({ dur: '12', fps: '2', out: clips.final })} />

/** Refused for its runtime: asking to install -- the `consent` preset's frame, the sheet's words over it. */
export const RefusedAskingToInstall = () => {
  const model = useVideoPreview({ ticks: 3, then: refused })
  useObserved(model)
  const consent = model.face().consent
  return (
    <Frame model={model}>
      {consent.shown ? (
        <div className="card" style={{ position: 'absolute', left: 300, top: 260, width: 580, padding: '18px 20px', display: 'flex', flexDirection: 'column', gap: 12, fontSize: 'var(--t-small)' }}>
          <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{consent.title}</h2>
          <InstallConsentSheet said={consent.said} body={consent.body} command={consent.command} />
        </div>
      ) : null}
    </Frame>
  )
}

/** Installing the video set, Stop beside it -- the `video:install` preset's frame. */
export const Installing = () => <Place ticks={3} then={refused} script={[[(video) => video.install(), 4]]} />

/** The install failed, in the CLI's words. */
export const InstallFailed = () => <Place ticks={3} then={refused} script={[[(video) => video.install(), 12]]} />

/** The install stopped before it finished. */
export const InstallStopped = () => <Place ticks={3} then={refused} script={[[(video) => video.install(), 2], [(video) => video.stop(), 0]]} />

/** Installed, the refused clip made again: the first install failed and was put away, Generate asked again, the second went in, and the rain run goes. */
export const InstalledClipMadeAgain = () => (
  <Place
    ticks={3}
    then={refused}
    script={[
      [(video) => video.install(), 12],
      [(video) => (video.installAct(), video.generate()), 3],
      [(video) => video.install(), 16],
    ]}
  />
)

/** With no apogee: the clips, which are files, and nothing generated. */
export const Missing = () => <Place apogee="missing" />

/** The Compositions tab: the slot the app root fills, its placeholder until 1l. */
export const Compositions = () => <Place then={(video) => video.pick('compositions')} />
