import type { JobStarting } from '../../../business/jobs/SupervisedJob'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { Symphony } from '../../../business/reads/Symphonies'
import type { FolderWriting } from '../../../infrastructure/process/StandInFolders'
import { Observable } from '../../styles/Observation/Observable'
import { AgentsViewModel } from './AgentsViewModel'
import type { DefinitionEditing, LibraryDoors, Ticks } from './DefinitionEditor'
import type { EditorFace, LibraryConfirm, LibraryConfirmWords, LibraryFace, LibraryRowFace, LibraryTab, MCPPageFace, SymphonyPageFace } from './LibraryFaces'
import { MCPViewModel } from './MCPViewModel'
import { imageTypes, SymphoniesViewModel } from './SymphoniesViewModel'

export type { LibraryTab } from './LibraryFaces'

const tabs: ReadonlyArray<readonly [LibraryTab, string]> = [
  ['agents', 'Agents'],
  ['symphonies', 'Symphonies'],
  ['mcp', 'MCP servers'],
]

/**
 * The Library place (design 1n): its three tabs over the Mac app's three
 * view-models (`AgentsViewModel`, `SymphoniesViewModel`, `MCPViewModel`), as
 * the Mac's `LibraryScreen` holds them -- the tab, the list, the page, the
 * editor sheet and its close prompt, the removal of a server -- with the
 * doors the composition fills: the toast, the shell's sheet and confirm
 * hosts. Plain TypeScript: it never imports React.
 */
export class LibraryViewModel extends Observable {
  readonly agents: AgentsViewModel
  readonly symphonies: SymphoniesViewModel
  readonly mcp: MCPViewModel
  tab: LibraryTab = 'agents'
  private readonly doors: LibraryDoors = {}

  constructor(runner: CommandRunning, starter: JobStarting, folders: FolderWriting, clock: Ticks) {
    super()
    this.agents = new AgentsViewModel(runner, folders, clock, this.doors)
    this.symphonies = new SymphoniesViewModel(runner, starter, folders, clock, this.doors)
    this.mcp = new MCPViewModel(runner, this.doors)
    for (const model of [this.agents, this.symphonies, this.mcp]) model.subscribe(() => this.changed())
  }

  set say(say: ((text: string) => void) | undefined) {
    this.doors.say = say
  }
  set present(present: ((sheet: string) => void) | undefined) {
    this.doors.present = present
  }
  set dismissSheet(dismiss: (() => void) | undefined) {
    this.doors.dismissSheet = dismiss
  }
  set ask(ask: ((confirm: string) => void) | undefined) {
    this.doors.ask = ask
  }

  connect(binary: string | undefined): void {
    this.agents.connect(binary)
    this.symphonies.connect(binary)
    this.mcp.connect(binary)
  }

  /** The place came into view: every tab's lists read (the Mac reads the servers only once the place shows, since listing connects to each). */
  async appeared(): Promise<void> {
    await this.agents.refresh()
    await this.symphonies.refresh()
    await this.mcp.refresh()
  }

  /** The old page's sample inputs and image (the prototype's own words, since no CLI document carries one), handed in by the composition as Voice's card is handed its sample. */
  useSamples(inputs: Readonly<Record<string, string>>, image: string): void {
    this.symphonies.setInputs(inputs)
    this.symphonies.sampleImage = image
  }

  pick(tab: LibraryTab): void {
    this.tab = tab
    this.changed()
  }

  /** A row pressed: a symphony's picks it; an agent's or a server's does nothing, as the old rows' empty `pick`. */
  pickRow(name: string): void {
    if (this.tab === 'symphonies') this.symphonies.select(name)
  }

  /** The view-model whose editor is open: the agents' or the symphonies'. */
  get editing(): DefinitionEditing | undefined {
    return this.agents.editor ? this.agents : this.symphonies.editor ? this.symphonies : undefined
  }

  setEditorText(text: string): void {
    this.editing?.setText(text)
  }

  save(): Promise<void> {
    return this.editing?.save() ?? Promise.resolve()
  }

  restore(): Promise<void> {
    return this.editing?.restore() ?? Promise.resolve()
  }

  requestClose(): void {
    this.editing?.requestClose()
  }

  /** A confirm's words, while it stands. */
  confirmWords(kind: LibraryConfirm): LibraryConfirmWords | undefined {
    if (kind === 'mcpRemove') {
      const words = this.mcp.removeWords
      return words ? { ...words, cancel: 'Cancel', alternative: '', confirm: 'Remove its entry', destructive: true } : undefined
    }
    const editing = this.editing
    const reason = editing?.closing
    const editor = editing?.editor
    if (!editor || reason === undefined) return undefined
    const title = editor.question(reason)
    const body = editor.explanation(reason)
    // A refused text in the file: put it back (the plain button), leave it (the destructive one), or keep editing; else Discard, destructive.
    return reason === 'refusedOnDisk' ? { title, body, cancel: 'Keep editing', alternative: 'Leave it', confirm: 'Put it back as it was', destructive: false } : { title, body, cancel: 'Keep editing', alternative: '', confirm: 'Discard', destructive: true }
  }

  /** A confirm answered: its yes, or its second way (`alternative`: Leave it). */
  async confirm(kind: LibraryConfirm, alternative: boolean): Promise<void> {
    if (kind === 'mcpRemove') {
      await this.mcp.confirmRemove()
      return
    }
    const editing = this.editing
    if (!editing || editing.closing === undefined) return
    if (alternative) editing.leaveAndClose()
    else if (editing.closing === 'refusedOnDisk') await editing.restoreAndClose()
    else editing.discardAndClose()
  }

  /** A confirm's Cancel: Keep editing, or the removal not made. */
  dismissPending(kind: LibraryConfirm): void {
    if (kind === 'mcpRemove') this.mcp.dismissRemove()
    else this.editing?.keepEditing()
  }

  // MARK: - The face

  face(): LibraryFace {
    const tab = this.tab
    const rows = tab === 'agents' ? this.agentRows() : tab === 'symphonies' ? this.symphonyRows() : this.serverRows()
    const note = (tab === 'agents' ? this.agents.emptyText : tab === 'symphonies' ? this.symphonies.emptyText : this.mcp.serversText) ?? ''
    const agent = tab === 'agents' ? this.agents.current : undefined
    const symphony = tab === 'symphonies' ? this.symphonies.current : undefined
    return {
      tabs: tabs.map(([value, label]) => ({ tab: value, label, current: value === tab })),
      newAgent: tab === 'agents',
      rows,
      note,
      emptyDetail: tab === 'agents' && !agent ? 'Choose an agent.' : tab === 'symphonies' && !symphony ? 'Choose a symphony.' : '',
      agent: agent
        ? {
            shown: true,
            name: agent.name,
            canDelete: !agent.bundled,
            description: agent.description,
            facts: `Model: ${agent.model === '' ? 'the default' : agent.model} · tools: ${agent.tools}${agent.collection === '' ? '' : ` · retrieves from ${agent.collection}`}`,
            files: [...agent.prompts, ...agent.schemas],
            runs: `apogee analyze --agent ${agent.name} runs it.`,
          }
        : { shown: false, name: '', canDelete: false, description: '', facts: '', files: [], runs: '' },
      symphony: this.symphonyPage(tab === 'symphonies'),
      mcp: this.serverPage(tab === 'mcp'),
    }
  }

  private agentRows(): LibraryRowFace[] {
    const current = this.agents.current?.name
    return this.agents.agents.map((agent) => ({ name: agent.name, meta: `${agent.bundled ? 'shipped' : 'yours'} · ${agent.tools}`, current: agent.name === current, pickable: false }))
  }

  private symphonyRows(): LibraryRowFace[] {
    const current = this.symphonies.current?.name
    return this.symphonies.symphonies.map((symphony) => {
      const count = symphony.stages.length
      const problems = symphony.problems.length
      return { name: symphony.name, meta: `${count} stage${count === 1 ? '' : 's'} · ${symphony.source}${problems === 0 ? '' : ` · ${problems} problem${problems === 1 ? '' : 's'}`}`, current: symphony.name === current, pickable: true }
    })
  }

  private serverRows(): LibraryRowFace[] {
    const current = this.mcp.current?.name
    return this.mcp.listed.map((server) => {
      const tools = server.tools.length
      return { name: server.name, meta: `${server.enabled ? server.state : 'disabled'} · ${tools} tool${tools === 1 ? '' : 's'}`, current: server.name === current, pickable: false }
    })
  }

  private symphonyPage(onTab: boolean): SymphonyPageFace {
    const model = this.symphonies
    const symphony = onTab ? model.current : undefined
    if (!symphony) {
      return { shown: false, name: '', canEdit: false, description: '', stages: [], note: '', inputDescription: '', input: '', image: { takes: false, chosen: '', types: '' }, canPlay: false, needsImage: '', playing: { shown: false, line: '' }, played: { shown: false, stages: [], output: '' } }
    }
    const progress = model.shownProgress
    const play = model.shownPlay
    const note = symphony.source === 'shipped' ? `A shipped starter: apogee symphonies edit ${symphony.name} makes it yours, at a prompt.` : symphony.source === 'config' ? `A config entry: apogee symphonies edit ${symphony.name} edits it, at a prompt.` : ''
    return {
      shown: true,
      name: symphony.name,
      canEdit: Symphony.isOwnFile(symphony),
      description: symphony.description,
      stages: symphony.stages.map((stage, index) => `${index + 1}. ${stage.name} · ${stage.plays === undefined ? `asks ${stage.role ?? '?'}` : `plays ${stage.plays}`}`),
      note,
      inputDescription: symphony.inputDescription,
      input: model.input,
      image: { takes: symphony.takesImage, chosen: symphony.takesImage ? (model.imageName ?? '') : '', types: `${imageTypes.slice(0, -1).join(', ')} or ${imageTypes.at(-1) ?? ''}` },
      canPlay: model.canPlay,
      needsImage: model.needsImage ? `${symphony.name} takes an image with its input: choose one to play it.` : '',
      playing: progress ? { shown: true, line: progress.busy ?? `apogee symphonies play ${symphony.name}` } : { shown: false, line: '' },
      played: play ? { shown: true, stages: play.stages.map((stage) => ({ label: `${stage.name} · ${stage.role} (${stage.backend})`, answer: stage.answer })), output: play.output } : { shown: false, stages: [], output: '' },
    }
  }

  private serverPage(onTab: boolean): MCPPageFace {
    const model = this.mcp
    const server = onTab ? model.current : undefined
    const presets = model.presets.map((preset) => (preset.refusal === undefined ? { name: preset.name, text: preset.description, refused: false } : { name: preset.name, text: preset.refusal, refused: true }))
    const presetsNote = model.presetsText ?? ''
    if (!server) return { shown: false, name: '', toggle: '', command: '', facts: '', presetsNote, presets }
    const tools = server.tools.length
    return {
      shown: true,
      name: server.name,
      toggle: server.enabled ? 'Disable' : 'Enable',
      command: [server.command, ...server.args].join(' '),
      facts: `${server.enabled ? server.state : 'disabled'} · ${tools} tool${tools === 1 ? '' : 's'}${tools === 0 ? '' : `: ${server.tools.join(', ')}`} · environment: ${server.envSet ? 'set (its values are never shown)' : 'none'}`,
      presetsNote,
      presets,
    }
  }

  /** The editor sheet's face, while one is open. */
  editorFace(): EditorFace {
    const editing = this.editing
    const editor = editing?.editor
    if (!editing || !editor) return { shown: false, title: '', path: '', text: '', saving: false, busy: '', verdict: { text: '', refused: false }, restore: { shown: false, enabled: false }, canSave: false }
    return {
      shown: true,
      title: editor.title,
      path: editor.path,
      text: editor.text,
      saving: editor.saving,
      busy: editor.saving ? "Saving: the editor waits for apogee's word on the file, then takes your typing again." : '',
      verdict: !editor.saving && editor.verdict !== undefined ? { text: editor.verdict, refused: editor.refused } : { text: '', refused: false },
      restore: { shown: editing.offersRestore, enabled: !editor.saving },
      canSave: !editor.saving && (editor.changed || editor.verdict !== undefined),
    }
  }
}
