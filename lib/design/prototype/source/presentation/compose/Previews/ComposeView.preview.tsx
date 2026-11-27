import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'
import { ComposeView } from '../Views/ComposeView'
import { capturedVoiceList, composeFiles, composeWorld, heatPumpFrame, moviesFolder, projects, recordedEdit } from './ComposeWorlds'
import { useComposePreview } from './ComposePreview'
import type { ComposePreviewOptions } from './ComposePreview'

/** The compositions on their own (design 1l), one preview per state a person sees differently, over the compositions' world: the old presets' frames, and each state the view-model reaches. */
const frame = { position: 'relative', width: 1180, height: 820, display: 'flex', flexDirection: 'column', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Frame({ model, children }: { readonly model: ComposeViewModel; readonly children?: ReactNode }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <ComposeView model={model} />
      {children}
    </div>
  )
}

function Place(options: ComposePreviewOptions) {
  return <Frame model={useComposePreview(options)} />
}

const planned = (compose: ComposeViewModel) => {
  compose.newComposition()
  compose.compose(true)
}
const tidesPreviewed = (compose: ComposeViewModel) => {
  compose.select(projects.tides)
  compose.editForm({ instruction: recordedEdit })
  void compose.previewEdit()
}

/** `compose`: the heat pump, made, paused 9.6 s in on its caption, Make changes open beside it. */
export const Composition = () => (
  <Place
    then={(compose) => {
      compose.select(projects.heatPump)
      compose.hold(heatPumpFrame)
    }}
  />
)

/** `compose:edit`: Tides, made, its edit typed and previewed -- the recorded dry run's diff, the spend, and Render saying it plans again. */
export const EditPreviewed = () => <Place then={tidesPreviewed} ticks={5} />

/** `compose:new`: a new composition, the recorded brief planned, nothing rendered. */
export const NewPlanned = () => <Place then={planned} ticks={12} />

/** `compose:tide`: the tide project, stopped at scene 5's clip -- its phases, its stop note, Resume, and Make changes locked. */
export const Stopped = () => <Place then={(compose) => compose.select(projects.tidePools)} />

/** `compose:differs`: the Tides edit rendered from its preview, the render's own plan come out otherwise, said above its changes. */
export const RenderedOtherwise = () => <Place then={tidesPreviewed} ticks={30} script={[[(compose) => compose.renderEdit(), 0]]} />

/** A new composition, its brief written, not planned yet. */
export const NewBrief = () => <Place then={(compose) => compose.newComposition()} />

/** A new composition planning: its one structured call, the Plan button gone. */
export const Planning = () => <Place then={planned} ticks={3} />

/** The plan rendering: scene 1's clip denoising, step 20 of 50, its narrations spoken, the edit pane locked while it renders. A still frame replays its 12 start ticks into the render too, so 2 more stand it 14 lines in. */
export const Rendering = () => <Place then={planned} ticks={12} script={[[(compose) => compose.compose(), 2]]} />

/** The tide resumed, then stopped at scene 6's clip: the note says the step it stopped on, Resume offered again. */
export const StoppedHere = () => (
  <Place
    then={(compose) => {
      compose.select(projects.tidePools)
      const tide = compose.selectedProject
      if (tide) compose.resume(tide)
    }}
    script={[[() => undefined, 16], [(compose) => compose.stop(), 1]]}
  />
)

/** An edit's preview planning on paper: at most 5 minutes, Stop beside it. */
export const Previewing = () => (
  <Place
    then={(compose) => {
      compose.select(projects.tides)
      compose.editForm({ instruction: recordedEdit })
      void compose.previewEdit()
    }}
  />
)

/** An edit's preview no recording answers: the run's failure, in the stand-in's words. */
export const PreviewFailed = () => (
  <Place
    then={(compose) => {
      compose.select(projects.heatPump)
      compose.editForm({ instruction: 'make the second scene show the indoor unit on the wall' })
      void compose.previewEdit()
    }}
    ticks={2}
  />
)

/** An edit of the heat pump, a re-roll ticked and its voice changed: the command names them, Preview offered. */
export const EditTyped = () => (
  <Place
    then={(compose) => {
      compose.select(projects.heatPump)
      compose.toggleRedo(4)
      compose.editForm({ voice: 'bf_emma', instruction: 'make the second scene show the indoor unit on the wall' })
    }}
  />
)

/** An edit's page: the night market's changes against the project it came from, and the chain back to it. */
export const EditPage = () => <Place then={(compose) => compose.select(projects.nightMarket2)} />

/** A new composition over the capture's store, its two Kokoro sets (`compose-voices-list.json`): the voice model picker, PyTorch first. */
export const VoiceModels = () => {
  const world = composeWorld()
  return <Place rules={{ ...world, reads: [...world.reads, { arguments: ['models', 'list'], answer: { stdout: capturedVoiceList } }] }} then={(compose) => compose.newComposition()} />
}

/** No compositions yet: the list says how a composition is changed, and nothing is shown. */
export const Empty = () => <Place folders={{ [moviesFolder]: [] }} />

/** The old four alone, as every preset's folder holds them: no Tides. */
export const OldFour = () => <Place folders={{ [moviesFolder]: composeFiles() }} />

/** No apogee: the projects are the person's files and read the same, but nothing plans or previews. */
export const NoApogee = () => <Place apogee="missing" then={(compose) => compose.newComposition()} />
