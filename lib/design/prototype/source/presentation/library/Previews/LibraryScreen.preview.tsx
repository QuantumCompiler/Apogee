import { LibraryScreen } from '../Views/LibraryScreen'
import { useLibraryPreview } from './LibraryPreview'
import type { LibraryPreviewOptions } from './LibraryPreview'

/** The place in a window-sized frame, as the shell draws a place. */
function Screen({ options }: { readonly options: LibraryPreviewOptions }) {
  const model = useLibraryPreview(options)
  return (
    <div className="ap" data-theme="dark" style={{ display: 'flex', flexDirection: 'column', height: 900, background: 'var(--surface-window)', color: 'var(--text-primary)' }}>
      <LibraryScreen model={model} />
    </div>
  )
}

const agents: LibraryPreviewOptions = {}
/** `library`: the agents tab, the CLI's eight in its order, triage's page by its name. */
export const Agents = () => <Screen options={agents} />

const agentsMissing: LibraryPreviewOptions = { apogee: 'missing' }
/** No apogee: no agents, the Mac's words where the list is, and "Choose an agent." */
export const AgentsNoApogee = () => <Screen options={agentsMissing} />

const agentsTypo: LibraryPreviewOptions = { apogee: 'config-typo' }
/** A config typo: the CLI's refusal under the list, no page. */
export const AgentsConfigTypo = () => <Screen options={agentsTypo} />

const played: LibraryPreviewOptions = { preset: 'library:symphonies', then: (library) => library.pick('symphonies'), script: [[(library) => library.symphonies.play(), 40]] }
/** `library:symphonies`: critique-revise played, each stage's answer and the output under Play. */
export const SymphoniesPlayed = () => <Screen options={played} />

const playing: LibraryPreviewOptions = { preset: 'library:symphonies', then: (library) => library.pick('symphonies'), script: [[(library) => library.symphonies.play(), 2]] }
/** A play running: the CLI's stage line under Play, Play off until it ends. */
export const SymphonyPlaying = () => <Screen options={playing} />

const shipped: LibraryPreviewOptions = { then: (library) => library.pick('symphonies') }
/** A shipped starter nobody has played: its stages, the starter's note and Play. */
export const SymphonyShipped = () => <Screen options={shipped} />

const playsAnother: LibraryPreviewOptions = {
  then: (library) => {
    library.pick('symphonies')
    library.pickRow('digest')
  },
}
/** A symphony a stage of which plays another: "1. summary · plays summarize-verify". */
export const SymphonyPlaysAnother = () => <Screen options={playsAnother} />

const configEntry: LibraryPreviewOptions = {
  then: (library) => {
    library.pick('symphonies')
    library.pickRow('quick-check')
  },
}
/** A config entry: the Mac's words for one, edited at a prompt; no input of its own, so Play waits. */
export const SymphonyConfigEntry = () => <Screen options={configEntry} />

const ownFile: LibraryPreviewOptions = {
  then: (library) => {
    library.pick('symphonies')
    library.pickRow('notes-digest')
  },
}
/** The person's own spec file: Edit… opens it in the app. */
export const SymphonyOwnFile = () => <Screen options={ownFile} />

const noImage: LibraryPreviewOptions = {
  preset: 'library:image',
  then: (library) => {
    library.pick('symphonies')
    library.pickRow('describe-answer')
  },
}
/** `library:image`: describe-answer takes an image and none is chosen: Choose an image…, Play off, and why. */
export const ImageNotChosen = () => <Screen options={noImage} />

const imageChosen: LibraryPreviewOptions = {
  preset: 'library:image',
  then: (library) => {
    library.pick('symphonies')
    library.pickRow('describe-answer')
    library.symphonies.chooseImage()
  },
}
/** The image chosen: its name, Remove and Choose another…, Play on. */
export const ImageChosen = () => <Screen options={imageChosen} />

const symphoniesMissing: LibraryPreviewOptions = { apogee: 'missing', then: (library) => library.pick('symphonies') }
/** No apogee on the symphonies tab: "Choose a symphony." */
export const SymphoniesNoApogee = () => <Screen options={symphoniesMissing} />

const servers: LibraryPreviewOptions = { preset: 'library:mcp', then: (library) => library.pick('mcp') }
/** `library:mcp`: code, connected with its two tools, and the presets Apogee ships. */
export const MCPServers = () => <Screen options={servers} />

const removed: LibraryPreviewOptions = {
  preset: 'library:mcp',
  then: async (library) => {
    library.pick('mcp')
    library.mcp.askToRemove()
    await library.confirm('mcpRemove', false)
  },
}
/** code removed: the CLI's line said, the list empty, the presets still under it. */
export const MCPServerRemoved = () => <Screen options={removed} />

const serversMissing: LibraryPreviewOptions = { preset: 'library:mcp', apogee: 'missing', then: (library) => library.pick('mcp') }
/** No apogee on the MCP tab: no servers and no presets, each said where it is drawn. */
export const MCPNoApogee = () => <Screen options={serversMissing} />

const serversTypo: LibraryPreviewOptions = { preset: 'library:mcp', apogee: 'config-typo', then: (library) => library.pick('mcp') }
/** A config typo on the MCP tab: the CLI's refusal for the list, and the stand-in's for the presets. */
export const MCPConfigTypo = () => <Screen options={serversTypo} />
