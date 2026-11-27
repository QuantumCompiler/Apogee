import { ConfirmHost } from '../../shell/Views/Hosts'
import { useObserved } from '../../styles/Observation/useObserved'
import type { GraphConfirm } from '../ViewModels/GraphFaces'
import type { GraphViewModel } from '../ViewModels/GraphViewModel'
import { GraphScreen } from '../Views/GraphScreen'
import { overtakenNode } from './GraphWorlds'
import { useGraphPreview } from './GraphPreview'
import type { GraphPreviewOptions } from './GraphPreview'

/** A confirm over the place, as the app root frames it in the shell's confirm host. */
function Confirm({ model, kind }: { readonly model: GraphViewModel; readonly kind: GraphConfirm }) {
  useObserved(model)
  const words = model.confirmWords(kind)
  if (!words) return null
  return (
    <ConfirmHost
      title={words.title}
      body={words.body}
      {...(words.cancel === '' ? {} : { cancel: { label: words.cancel, run: () => model.dismissDelete() } })}
      confirm={{ label: words.confirm, run: () => (kind === 'graphDelete' ? void model.confirmDelete() : model.dismissProblem()), destructive: words.destructive }}
    />
  )
}

function Screen({ options }: { readonly options: GraphPreviewOptions }) {
  const model = useGraphPreview(options)
  return (
    <div className="ap" data-theme="dark" style={{ position: 'relative', display: 'flex', flexDirection: 'column', height: 900, background: 'var(--surface-window)', color: 'var(--text-primary)' }}>
      <GraphScreen model={model} />
      <Confirm model={model} kind="graphDelete" />
      <Confirm model={model} kind="explainRefused" />
    </div>
  )
}

const functions: GraphPreviewOptions = {}
/** `graph`: the Functions view of the recorded demo graph, Runner.run_batch open. */
export const FunctionsView = () => <Screen options={functions} />

const walked: GraphPreviewOptions = { then: (graph) => graph.select('ingest.cli.main') }
/** main chosen: its two calls at full strength, the rest faded. */
export const NodeChosen = () => <Screen options={walked} />

const files: GraphPreviewOptions = { then: (graph) => graph.show('files') }
/** The Files view: the four files and their imports, IMPORTED BY and IMPORTS. */
export const FilesView = () => <Screen options={files} />

const prose: GraphPreviewOptions = { then: (graph) => graph.show('prose') }
/** `graph:prose`: the notes collection's prose graph, its busiest entity explained. */
export const ProseGraph = () => <Screen options={prose} />

const more: GraphPreviewOptions = { then: (graph) => graph.toggleMore() }
/** The More menu open: export, the report, dedupe and delete. */
export const MoreOpen = () => <Screen options={more} />

const deleteAsked: GraphPreviewOptions = { then: (graph) => graph.askToDelete() }
/** Delete the graph asked, in the CLI's words. */
export const DeleteAsked = () => <Screen options={deleteAsked} />

const deleted: GraphPreviewOptions = {
  then: async (graph) => {
    graph.askToDelete()
    await graph.confirmDelete()
  },
}
/** The graph deleted: the gone state, Rebuild offered, More off. */
export const GraphDeleted = () => <Screen options={deleted} />

const rebuilt: GraphPreviewOptions = {
  then: async (graph) => {
    graph.askToDelete()
    await graph.confirmDelete()
  },
  script: [[(graph) => graph.rebuild(), 3]],
}
/** Rebuild after the delete: the graph back. */
export const Rebuilt = () => <Screen options={rebuilt} />

const refused: GraphPreviewOptions = {
  then: async (graph) => {
    graph.overtaken()
    await graph.select(overtakenNode)
  },
}
/** `graph:refused`: Runner._fetch clicked in a picture an update elsewhere overtook, its explain refused over the kept canvas. */
export const ExplainRefused = () => <Screen options={refused} />

const missing: GraphPreviewOptions = { apogee: 'missing' }
/** No apogee: the canvas empty and no words, More off, as the old place has no missing state. */
export const NoApogee = () => <Screen options={missing} />

const typo: GraphPreviewOptions = { apogee: 'config-typo' }
/** A config typo: the CLI's refusal where the canvas stands. */
export const ConfigTypo = () => <Screen options={typo} />
