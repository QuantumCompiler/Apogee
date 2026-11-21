import type { ReactNode } from 'react'
import type { Specimen } from '../../styles/Specimens/Specimen'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'
import { EditPane } from '../Views/EditPane'
import { NewComposition } from '../Views/NewComposition'
import { ProjectList } from '../Views/ProjectList'
import { Timeline } from '../Views/ProjectPage'
import { heatPumpFrame, projects } from './ComposeWorlds'
import { useComposePreview } from './ComposePreview'

/**
 * The compositions' specimens for the gallery's Components pages (design
 * 1q): their parts over the compositions' world, as their previews draw
 * them, with no frame or theme of their own.
 */
function Watched({ model, children }: { readonly model: ComposeViewModel; readonly children: ReactNode }) {
  useObserved(model)
  return <div style={{ display: 'flex', flexDirection: 'column', gap: 16, maxWidth: 860 }}>{children}</div>
}

const heatPump = (compose: ComposeViewModel) => {
  compose.select(projects.heatPump)
  compose.hold(heatPumpFrame)
}

function List() {
  const model = useComposePreview({ then: (compose) => compose.newComposition() })
  return (
    <Watched model={model}>
      <ProjectList model={model} />
    </Watched>
  )
}

function Plan() {
  const model = useComposePreview({
    then: (compose) => {
      compose.newComposition()
      compose.compose(true)
    },
    ticks: 12,
  })
  return (
    <Watched model={model}>
      <NewComposition model={model} />
    </Watched>
  )
}

function Track() {
  const model = useComposePreview({ then: heatPump })
  return (
    <Watched model={model}>
      <Timeline model={model} />
    </Watched>
  )
}

function Changes() {
  const model = useComposePreview({ then: heatPump })
  return (
    <Watched model={model}>
      <EditPane model={model} />
    </Watched>
  )
}

export const specimens: readonly Specimen[] = [
  { name: 'Compositions · the list, newest first', section: 'media', module: 'compose', item: '1l', render: List },
  { name: 'A new composition · planned on paper', section: 'media', module: 'compose', item: '1l', render: Plan },
  { name: 'A composition’s timeline and scenes', section: 'media', module: 'compose', item: '1l', render: Track },
  { name: 'The edit preview · Make changes', section: 'media', module: 'compose', item: '1l', render: Changes },
]
