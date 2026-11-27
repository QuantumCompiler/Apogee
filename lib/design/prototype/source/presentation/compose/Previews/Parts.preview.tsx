import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'
import { EditPane } from '../Views/EditPane'
import { CompositionSettings, NewComposition } from '../Views/NewComposition'
import { ProjectList } from '../Views/ProjectList'
import { ProjectPage, Timeline } from '../Views/ProjectPage'
import { heatPumpFrame, projects } from './ComposeWorlds'
import { useComposePreview } from './ComposePreview'

/** Each part of the compositions on its own (design 1l), over the compositions' world, as the tab draws it. */
const frame = { display: 'flex', flexDirection: 'column', gap: 16, width: 860, padding: 20, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Part({ model, children }: { readonly model: ComposeViewModel; readonly children: ReactNode }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {children}
    </div>
  )
}

const heatPump = (compose: ComposeViewModel) => {
  compose.select(projects.heatPump)
  compose.hold(heatPumpFrame)
}

/** The list alone: the draft, the old four and Tides, newest first, and what the scan passed over. */
export const List = () => {
  const model = useComposePreview({ then: (compose) => compose.newComposition() })
  return (
    <Part model={model}>
      <ProjectList model={model} />
    </Part>
  )
}

/** The new composition's middle, planned: the plan, its spend, Render and Plan again. */
export const Plan = () => {
  const model = useComposePreview({
    then: (compose) => {
      compose.newComposition()
      compose.compose(true)
    },
    ticks: 12,
  })
  return (
    <Part model={model}>
      <NewComposition model={model} />
    </Part>
  )
}

/** The new composition's settings: the planner, the pipeline, the voice and its model, the bed, the assets, captions, Save as and the command. */
export const Settings = () => {
  const model = useComposePreview({
    then: (compose) => {
      compose.newComposition()
      compose.addMusic()
      compose.addAssets()
    },
  })
  return (
    <Part model={model}>
      <CompositionSettings model={model} />
    </Part>
  )
}

/** A project's page alone: the heat pump, made, its player paused on a caption. */
export const Page = () => {
  const model = useComposePreview({ then: heatPump })
  return (
    <Part model={model}>
      <ProjectPage model={model} />
    </Part>
  )
}

/** The timeline alone: the heat pump's six scenes, the narration, the cues and the bed, the playhead at 9.6 s. */
export const Track = () => {
  const model = useComposePreview({ then: heatPump })
  return (
    <Part model={model}>
      <Timeline model={model} />
    </Part>
  )
}

/** Make changes alone: the heat pump's, nothing changed yet, Preview off. */
export const Changes = () => {
  const model = useComposePreview({ then: heatPump })
  return (
    <Part model={model}>
      <EditPane model={model} />
    </Part>
  )
}
