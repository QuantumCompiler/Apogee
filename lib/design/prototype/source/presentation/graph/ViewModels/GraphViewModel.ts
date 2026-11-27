import type { JobNotes } from '../../../business/jobs/JobNotes'
import { JobProgress } from '../../../business/jobs/JobProgress'
import type { JobRunning, JobStarting } from '../../../business/jobs/SupervisedJob'
import { CommandOutcome } from '../../../business/reads/CommandRunning'
import type { CommandRunning } from '../../../business/reads/CommandRunning'
import { GraphExplain, GraphNode, GraphQuery, GraphReport, GraphStats } from '../../../business/reads/Graph'
import type { GraphEdge, GraphRelation } from '../../../business/reads/Graph'
import { ReadOutcome } from '../../../business/reads/ReadRunner'
import { tilde } from '../../styles/Media/MediaWords'
import { Observable } from '../../styles/Observation/Observable'
import type { CardFace, CodeFace, DedupeSheetFace, EntityFace, GraphConfirm, GraphConfirmWords, GraphFace, GraphView, MoreItemFace, ProseFace, SaveKind, SavePanelFace, TreeRowFace } from './GraphFaces'
import { GraphLayout } from './GraphLayout'
import type { Placed } from './GraphLayout'

export type { GraphView, SaveKind } from './GraphFaces'

/**
 * The place's world, handed in by its owner as the old prototype holds it in
 * its state: the code graph and the collection whose prose graph it draws,
 * the questions whose pictures the Functions and Files views show (the
 * prototype has no search; the Mac's is new design here), the node each view
 * opens on (the old `gNode`s), and the folder a save goes to.
 */
export interface GraphWorld {
  readonly code: string
  readonly prose: string
  readonly functionsQuestion: string
  readonly filesQuestion: string
  readonly functionsStart: string
  readonly filesStart: string
  readonly documents: string
}

/** Where a dedupe stands: the Mac's `Dedupe`, its dry run first, the merge only on the person's word. */
type Dedupe =
  | { readonly case: 'planning' }
  | { readonly case: 'planned'; readonly text: string }
  | { readonly case: 'merging' }
  | { readonly case: 'merged'; readonly text: string }
  | { readonly case: 'failed'; readonly message: string }

/** A view's picture: the nodes it draws and the edges between them, placed on the canvas. */
interface Picture {
  readonly nodes: readonly GraphNode[]
  readonly edges: readonly GraphEdge[]
  readonly placed: ReadonlyMap<string, Placed>
}

const views: ReadonlyArray<readonly [GraphView, string]> = [
  ['functions', 'Functions'],
  ['files', 'Files'],
  ['prose', 'Collection'],
]

const moreItems: readonly MoreItemFace[] = [
  { label: 'Export as HTML page…', rule: false, destructive: false, act: 'html' },
  { label: 'Export as GraphML…', rule: false, destructive: false, act: 'graphml' },
  { label: 'Export as Mermaid diagram…', rule: false, destructive: false, act: 'mermaid' },
  { label: 'Save the report…', rule: false, destructive: false, act: 'report' },
  { label: '', rule: true, destructive: false, act: '' },
  { label: 'Dedupe…', rule: false, destructive: false, act: 'dedupe' },
  { label: 'Delete the graph…', rule: false, destructive: true, act: 'delete' },
]

const extensions: Readonly<Record<SaveKind, string>> = { html: 'html', graphml: 'graphml', mermaid: 'mmd', report: 'md' }
const plural = (count: number, word: string) => `${count} ${word}${count === 1 ? '' : 's'}`
const noCard: CardFace = { shown: false, kind: '', label: '', where: '', sig: '', inLabel: '', outLabel: '', callers: [], callees: [] }
const noEntity: EntityFace = { shown: false, kind: '', name: '', description: '', degree: '', communities: [], decisions: [], relations: [] }

/** A relation's heading, read as a person says it: the Mac's `GraphExplain.Relation.heading`, in the old pane's capitals. */
function heading(relation: GraphRelation): string {
  const words: Readonly<Record<string, string>> = { 'calls:in': 'called by', 'calls:out': 'calls', 'defined_in:in': 'defines', 'defined_in:out': 'defined in', 'imports:in': 'imported by', 'imports:out': 'imports' }
  const said = words[`${relation.relation}:${relation.direction}`] ?? (relation.direction === 'in' ? `${relation.relation} (in)` : relation.relation)
  return said.toUpperCase()
}

/**
 * The Code graph place (design 1o): the Mac app's `GraphViewModel`
 * (`GraphViewModel.swift`) -- the graph's counts (`graph stats`), its
 * neighbourhood laid out (`graph query`, `GraphLayout`), a node's neighbours,
 * a collection's report (`graph report`) with its busiest entity explained
 * (`graph explain`), Rebuild supervised, and the More menu: export and the
 * report saved where the save panel says, dedupe's dry run then its merge,
 * and delete asked first -- drawn as the old prototype draws the place: the
 * Functions and Files views of the code graph with its file tree, and the
 * collection's prose graph. A node's card is the picture's own, as the old
 * one is; `graph explain` is asked of a node only in a picture an update
 * elsewhere has overtaken (the old `gStale`), where its refusal is the place's
 * alert over the kept canvas and its OK the selection's Clear (the Mac's 6h).
 * Find communities and Open in editor stay the old toasts. Plain TypeScript.
 */
export class GraphViewModel extends Observable {
  view: GraphView = 'functions'
  /** Each graph's counts, by name. */
  readonly stats = new Map<string, GraphStats>()
  functions: GraphQuery | undefined = undefined
  files: GraphQuery | undefined = undefined
  report: GraphReport | undefined = undefined
  /** The prose graph's busiest entity, explained. */
  entity: GraphExplain | undefined = undefined
  selected: string | undefined = undefined
  /** Why the graph could not be read, in the CLI's words. */
  failure: string | undefined = undefined
  /** The graphs the CLI said it deleted: the stand-in keeps no state, so its word is kept beside the reads until a rebuild. */
  readonly deleted = new Set<string>()
  /** A picture an update elsewhere has overtaken (the old `gStale`): a node clicked is explained first. */
  isOvertaken = false
  /** Why a clicked node could not be explained, in the CLI's words, and which. */
  problem: string | undefined = undefined
  refused: string | undefined = undefined
  /** A rebuild or a merge running now. */
  building: JobProgress | undefined = undefined
  /** What the last run said on stderr in the CLI's house voice, for the graph it named, until Dismiss, the next run or another graph shown (GUI M22). */
  doneNotes: { readonly graph: string; readonly title: string; readonly notes: JobNotes } | undefined = undefined
  moreOpen = false
  /** The save panel's kind and file, while it is open. */
  saving: { readonly kind: SaveKind; readonly graph: string; readonly file: string } | undefined = undefined
  dedupe: Dedupe | undefined = undefined
  /** The graph the dedupe sheet is about. */
  dedupeGraph = ''
  /** The graph whose delete waits on the person's word. */
  pendingDelete: string | undefined = undefined

  /** The doors the composition fills: the toast, the shell's sheet and confirm hosts and its menu scrim, and the session "Ask about this" opens. */
  say?: (text: string) => void
  present?: (sheet: string) => void
  dismissSheet?: () => void
  ask?: (confirm: GraphConfirm) => void
  menuChanged?: (owner: string | undefined) => void
  askAbout?: (file: string, draft: string) => void

  private world: GraphWorld | undefined = undefined
  private binary: string | undefined = undefined
  private job: JobRunning | undefined = undefined
  private pictures: Partial<Record<'functions' | 'files', Picture>> = {}

  constructor(
    private readonly runner: CommandRunning,
    private readonly starter: JobStarting,
  ) {
    super()
  }

  /** The place's world, as the composition hands it in; the Functions view opens on its node. */
  useWorld(world: GraphWorld): void {
    this.world = world
    if (this.selected === undefined && this.view === 'functions') this.selected = world.functionsStart
    this.changed()
  }

  connect(binary: string | undefined): void {
    this.binary = binary
    this.changed()
  }

  /** The place came into view: the shown graph read again. */
  async appeared(): Promise<void> {
    await this.refresh()
  }

  /** The graph the view shows: the code graph, or the collection's. */
  get graph(): string {
    return this.view === 'prose' ? (this.world?.prose ?? '') : (this.world?.code ?? '')
  }

  /**
   * The shown graph's counts, then its pictures -- the Functions and Files
   * views' questions -- or, for a collection's, its report and its busiest
   * entity explained.
   */
  async refresh(): Promise<void> {
    const binary = this.binary
    const world = this.world
    if (binary === undefined || world === undefined) return
    // The view as it was asked: a view picked while these reads run reads its own.
    const view = this.view
    const graph = this.graph
    const stats = await this.runner.read(binary, ['graph', 'stats', graph])
    const document = ReadOutcome.documentOf(stats)
    if (!document) {
      if (view === this.view) this.failure = ReadOutcome.failure(stats)
      this.changed()
      return
    }
    this.failure = undefined
    const read = GraphStats.read(document)
    this.stats.set(graph, read)
    if (view === 'prose') {
      const report = read.built ? ReadOutcome.documentOf(await this.runner.read(binary, ['graph', 'report', '--graph', graph])) : undefined
      this.report = report ? GraphReport.read(report) : undefined
      const busiest = this.report?.hubs[0]?.node.name
      const explained = busiest === undefined ? undefined : ReadOutcome.documentOf(await this.runner.read(binary, ['graph', 'explain', '--graph', graph, '--', busiest]))
      this.entity = explained ? GraphExplain.read(explained) : undefined
    } else {
      this.functions = await this.picture(binary, graph, world.functionsQuestion)
      this.files = await this.picture(binary, graph, world.filesQuestion)
      this.pictures = {}
    }
    this.changed()
  }

  /** `graph query --graph <graph> -- <question>`: the neighbourhood the question names. */
  private async picture(binary: string, graph: string, question: string): Promise<GraphQuery | undefined> {
    const outcome = await this.runner.read(binary, ['graph', 'query', '--graph', graph, '--', question])
    const document = ReadOutcome.documentOf(outcome)
    return document ? GraphQuery.read(document) : undefined
  }

  /** A view's picture: Functions draws the code entities and their calls, Files the files and their imports, each placed by `GraphLayout`. */
  private shownPicture(view: 'functions' | 'files'): Picture {
    const kept = this.pictures[view]
    if (kept) return kept
    const query = view === 'functions' ? this.functions : this.files
    const nodes = (query?.nodes ?? []).filter((node) => (view === 'files') === (node.type === 'file'))
    const names = new Set(nodes.map((node) => node.name))
    const relation = view === 'files' ? 'imports' : 'calls'
    // Laid out over every relation the document holds between the drawn nodes, as the Mac lays out the query whole (a method defined in its class sits by it); drawn are the view's own.
    const between = (query?.edges ?? []).filter((edge) => names.has(edge.from) && names.has(edge.to))
    const edges = between.filter((edge) => edge.relation === relation)
    const picture = { nodes, edges, placed: GraphLayout.place([...names], between.map((edge) => [edge.from, edge.to] as const)) }
    this.pictures[view] = picture
    return picture
  }

  /** Functions, Files or the collection's graph: the old `showFns`, `showFiles` and `showProse`, each view opening on its node. */
  async show(view: GraphView): Promise<void> {
    this.closeMore()
    this.view = view
    if (this.doneNotes?.graph !== this.graph) this.doneNotes = undefined
    this.selected = view === 'functions' ? this.world?.functionsStart : view === 'files' ? this.world?.filesStart : undefined
    this.changed()
    await this.refresh()
  }

  /**
   * A node chosen. In a picture an update elsewhere overtook, it is explained
   * first (`graph explain --graph <graph> -- <name>`, the Mac's `select`): a
   * refusal is said in the place's alert, in the CLI's words, the canvas
   * kept and the node highlighted with no card.
   */
  async select(name: string): Promise<void> {
    this.selected = name
    this.changed()
    const binary = this.binary
    if (!this.isOvertaken || this.view === 'prose' || binary === undefined) return
    // Only a node the picture draws is clicked: one it does not is explained of nobody.
    if (!this.shownPicture(this.view).nodes.some((node) => node.name === name)) return
    const outcome = await this.runner.read(binary, ['graph', 'explain', '--graph', this.graph, '--', name])
    if (this.selected !== name) return
    const document = ReadOutcome.documentOf(outcome)
    if (document && GraphExplain.read(document)) return
    this.problem = ReadOutcome.failure(outcome) ?? `${name} could not be explained`
    this.refused = name
    this.changed()
    this.ask?.('explainRefused')
  }

  /** The notes' card dismissed (GUI M22). */
  dismissNotes(): void {
    this.doneNotes = undefined
    this.changed()
  }

  /** The alert's OK: the refusal's Clear -- the refused node's selection goes with it, the picture stays. */
  dismissProblem(): void {
    if (this.refused !== undefined && this.selected === this.refused) this.selected = undefined
    this.problem = undefined
    this.refused = undefined
    this.changed()
  }

  /** The picture an update elsewhere overtook: what graph:refused opens on (the old `gStale`), until a rebuild reads the graph again. */
  overtaken(): void {
    this.isOvertaken = true
    this.changed()
  }

  /** A file in the tree: the first node of the shown picture written in it, chosen; a file with none, nothing. */
  async pickFile(path: string): Promise<void> {
    if (this.view === 'prose') return
    const hit = this.shownPicture(this.view).nodes.find((node) => node.file === path)
    if (hit) await this.select(hit.name)
  }

  private get selectedNode(): GraphNode | undefined {
    if (this.view === 'prose' || this.selected === undefined) return undefined
    return this.shownPicture(this.view).nodes.find((node) => node.name === this.selected)
  }

  /** Ask about this: a new session with the node's file attached and the old draft, opened by the composition. */
  askAboutThis(): void {
    const node = this.selectedNode
    if (!node?.file) return
    const source = this.stats.get(this.graph)?.sources[0]
    this.askAbout?.(source === undefined ? node.file : `${source}/${node.file}`, `Explain ${GraphNode.label(node)} and what depends on it.`)
  }

  /** Open in editor: the old toast, naming where. */
  openInEditor(): void {
    const node = this.selectedNode
    if (!node) return
    this.say?.(`Opened ${this.view === 'files' ? node.name : (GraphNode.place(node) ?? node.name)} in your editor (prototype)`)
  }

  /** Find communities: the old toast, of the collection's graph, the one place it shows. */
  findCommunities(): void {
    this.say?.(`apogee graph communities ${this.world?.prose ?? ''}: the utility model summarises each group (prototype)`)
  }

  // MARK: - The More menu

  toggleMore(): void {
    this.moreOpen = !this.moreOpen
    this.changed()
    this.menuChanged?.(this.moreOpen ? 'graphMore' : undefined)
  }

  closeMore(): void {
    if (!this.moreOpen) return
    this.moreOpen = false
    this.changed()
    this.menuChanged?.(undefined)
  }

  /** An item of the More menu. */
  act(item: MoreItemFace['act']): void {
    if (item === 'html' || item === 'graphml' || item === 'mermaid') this.exportAs(item)
    else if (item === 'report') this.saveReport()
    else if (item === 'dedupe') void this.planDedupe()
    else if (item === 'delete') this.askToDelete()
  }

  /** Export as …: the save panel, offering the CLI's own file name, `<graph>.<ext>`. */
  exportAs(kind: 'html' | 'graphml' | 'mermaid'): void {
    this.openSave(kind, `${this.graph}.${extensions[kind]}`)
  }

  /** Save the report…: the save panel, offering `<graph>-report.md`. */
  saveReport(): void {
    this.openSave('report', `${this.graph}-report.md`)
  }

  private openSave(kind: SaveKind, file: string): void {
    this.closeMore()
    this.saving = { kind, graph: this.graph, file }
    this.changed()
    this.present?.('savePanel')
  }

  setSaveFile(file: string): void {
    if (!this.saving) return
    this.saving = { ...this.saving, file }
    this.changed()
  }

  private saveArguments(kind: SaveKind, graph: string, file: string): string[] {
    const path = `${this.world?.documents ?? ''}/${file}`
    return kind === 'report' ? ['graph', 'report', '--graph', graph, '--out', path] : ['graph', 'export', kind, '--graph', graph, '-o', path]
  }

  cancelSave(): void {
    this.saving = undefined
    this.changed()
    this.dismissSheet?.()
  }

  /** Save: `graph export <format> --graph <graph> -o <file>` or `graph report --graph <graph> --out <file>`, the CLI writing it, its first line said. */
  async save(): Promise<void> {
    const saving = this.saving
    const binary = this.binary
    const file = saving?.file.trim() ?? ''
    if (!saving || file === '' || binary === undefined) return
    this.saving = undefined
    this.changed()
    this.dismissSheet?.()
    const outcome = await this.runner.command(binary, this.saveArguments(saving.kind, saving.graph, file))
    this.say?.(outcome.case === 'done' ? outcome.stdout.split('\n')[0] || 'Done' : (CommandOutcome.failure(outcome) ?? 'Not saved'))
  }

  /**
   * Dedupe…: its dry run first -- `graph dedupe <graph> --dry-run`, which
   * changes nothing -- what it printed shown before anything merges.
   */
  async planDedupe(): Promise<void> {
    const binary = this.binary
    if (binary === undefined || this.job !== undefined) return
    this.closeMore()
    const graph = this.graph
    this.dedupeGraph = graph
    this.dedupe = { case: 'planning' }
    this.changed()
    this.present?.('dedupe')
    const outcome = await this.runner.command(binary, ['graph', 'dedupe', graph, '--dry-run'])
    this.dedupe = outcome.case === 'done' ? { case: 'planned', text: outcome.stdout.replace(/^\n+|\n+$/g, '') } : { case: 'failed', message: CommandOutcome.failure(outcome) ?? 'the dry run did not run' }
    this.changed()
  }

  /** Whether the dry run named entities to merge: a line the CLI starts `Would merge into` (`graph.cpp`'s dedupe). */
  get dedupeMerges(): boolean {
    return this.dedupe?.case === 'planned' && this.dedupe.text.split('\n').some((line) => line.startsWith('Would merge into '))
  }

  /** Merge them: `graph dedupe <graph>`, supervised, then read again, its lines kept on the sheet. */
  confirmDedupe(): void {
    const binary = this.binary
    if (!this.dedupeMerges || binary === undefined) return
    this.dedupe = { case: 'merging' }
    this.changed()
    this.run(['graph', 'dedupe', this.dedupeGraph], this.dedupeGraph, 'What the merge said', (progress) => {
      const status = progress.status
      this.dedupe = status.case === 'done' ? { case: 'merged', text: progress.notices.join('\n') } : { case: 'failed', message: status.case === 'failed' ? status.message : 'the merge was stopped' }
    })
  }

  /** Cancel or Done: the sheet closed -- never while it merges. */
  dismissDedupe(): void {
    if (this.dedupe?.case === 'merging') return
    this.dedupe = undefined
    this.changed()
    this.dismissSheet?.()
  }

  /** Delete the graph…: asked first. */
  askToDelete(): void {
    this.closeMore()
    this.pendingDelete = this.graph
    this.changed()
    this.ask?.('graphDelete')
  }

  dismissDelete(): void {
    this.pendingDelete = undefined
    this.changed()
  }

  /** `graph delete <graph>` once the person said yes, read back, the CLI's own sentence said; the graph gone until a rebuild. */
  async confirmDelete(): Promise<void> {
    const name = this.pendingDelete
    const binary = this.binary
    if (name === undefined || binary === undefined) return
    this.pendingDelete = undefined
    this.changed()
    const outcome = await this.runner.command(binary, ['graph', 'delete', name])
    if (outcome.case === 'done') this.deleted.add(name)
    await this.refresh()
    this.say?.(outcome.case === 'done' ? outcome.stdout.split('\n')[0] || 'Deleted' : (CommandOutcome.failure(outcome) ?? 'Not deleted'))
  }

  /**
   * Rebuild: `graph update <graph>` where it has sources, else `graph build
   * <graph>`, supervised; once it is done the graph is back and read again,
   * its last line said.
   */
  rebuild(): void {
    const graph = this.graph
    const sourced = (this.stats.get(graph)?.sources.length ?? 0) > 0
    this.run(sourced ? ['graph', 'update', graph] : ['graph', 'build', graph], graph, 'What the rebuild said', (progress) => {
      if (progress.status.case === 'done') {
        this.deleted.delete(graph)
        this.isOvertaken = false
        this.say?.(progress.notices.at(-1) ?? 'Built')
      } else this.say?.(progress.status.case === 'failed' ? progress.status.message : 'Stopped')
    })
  }

  /** Runs `args` supervised, then reads the graph again and lands what it said; a run that succeeded keeps its stderr's notes under `title` for `named`, the graph it ran on (GUI M22). */
  private run(args: readonly string[], named: string, title: string, ended: (progress: JobProgress) => void): void {
    const binary = this.binary
    if (binary === undefined || this.job !== undefined) return
    this.doneNotes = undefined
    const progress = new JobProgress()
    this.building = progress
    const job = this.starter.startJob(binary, args, false)
    this.job = job
    this.changed()
    void (async () => {
      for await (const update of job.updates) {
        progress.apply(update)
        this.changed()
      }
      this.job = undefined
      this.building = undefined
      if (progress.status.case === 'done' && !progress.notes.isEmpty) this.doneNotes = { graph: named, title, notes: progress.notes }
      ended(progress)
      await this.refresh()
    })()
  }

  // MARK: - The face

  /**
   * Why the graph is not drawn, in the CLI's or the Mac's words; `undefined`
   * when it is. With no apogee the canvas stands empty with no words, More
   * off: the old place has no missing state (the document's default; the
   * Mac's line is a follow-up's), and nothing was read to act on.
   */
  private get goneText(): string | undefined {
    if (this.binary === undefined) return ''
    if (this.failure !== undefined) return this.failure
    const graph = this.graph
    const stats = this.stats.get(graph)
    if (this.deleted.has(graph) || (stats !== undefined && !stats.built)) return `${graph} has no graph built. Rebuild parses its sources again -- no model, no key, no network.`
    return undefined
  }

  private get headerLine(): string {
    const graph = this.graph
    const stats = this.stats.get(graph)
    if (this.deleted.has(graph) || (stats !== undefined && !stats.built)) return '0 nodes · 0 edges · 0 files'
    if (!stats || this.failure !== undefined || this.binary === undefined) return ''
    if (stats.isProse) return `${graph} · ${plural(stats.nodes, 'entity').replace('entitys', 'entities')} · ${plural(stats.edges, 'relation')} · ${stats.communities} ${stats.communities === 1 ? 'community' : 'communities'}`
    const source = stats.sources[0]
    return `${source === undefined ? graph : tilde(source)} · ${plural(stats.codeFiles, 'file')} · ${plural(stats.nodes, 'symbol')}`
  }

  face(): GraphFace {
    const gone = this.goneText
    const prose = this.world?.prose ?? ''
    return {
      header: { line: this.headerLine },
      notes: this.doneNotes !== undefined && this.doneNotes.graph === this.graph ? { shown: true, title: this.doneNotes.title, lines: this.doneNotes.notes.shown } : { shown: false, title: '', lines: [] },
      views: views.map(([view, label]) => ({ view, label: view === 'prose' ? `${label} · ${prose}` : label, current: view === this.view })),
      rebuild: { enabled: this.binary !== undefined && this.building === undefined },
      findCommunities: this.view === 'prose' && gone === undefined,
      more: { open: this.moreOpen, enabled: gone === undefined, items: moreItems },
      gone: { shown: gone !== undefined, text: gone ?? '' },
      code: this.view === 'prose' || gone !== undefined ? { shown: false, tree: [], nodes: [], edges: [], card: noCard } : this.codeFace(this.view),
      prose: this.view === 'prose' && gone === undefined ? this.proseFace() : { shown: false, summary: '', communitiesTotal: 0, communities: [], decisionsTotal: 0, decisions: [], hubs: [], entity: noEntity },
    }
  }

  private codeFace(view: 'functions' | 'files'): CodeFace {
    const picture = this.shownPicture(view)
    const selected = picture.nodes.find((node) => node.name === this.selected)
    const near = new Set(picture.edges.flatMap((edge) => (edge.from === this.selected ? [edge.to] : edge.to === this.selected ? [edge.from] : [])))
    const where = (node: GraphNode) => (view === 'files' ? plural(GraphNode.lines(node) ?? 0, 'line') : (GraphNode.place(node) ?? ''))
    const neighbour = (name: string) => {
      const node = picture.nodes.find((candidate) => candidate.name === name)
      return { name, label: node ? GraphNode.label(node) : name }
    }
    const card: CardFace =
      selected && this.selected !== this.refused
        ? {
            shown: true,
            kind: selected.type.toUpperCase(),
            label: GraphNode.label(selected),
            where: view === 'files' ? `${selected.name} · ${plural(GraphNode.lines(selected) ?? 0, 'line')}` : (GraphNode.place(selected) ?? ''),
            sig: selected.description,
            inLabel: view === 'files' ? 'IMPORTED BY' : 'CALLED BY',
            outLabel: view === 'files' ? 'IMPORTS' : 'CALLS',
            callers: picture.edges.filter((edge) => edge.to === selected.name).map((edge) => neighbour(edge.from)),
            callees: picture.edges.filter((edge) => edge.from === selected.name).map((edge) => neighbour(edge.to)),
          }
        : noCard
    return {
      shown: true,
      tree: this.tree(selected?.file),
      nodes: picture.nodes.map((node) => {
        const at = picture.placed.get(node.name) ?? { x: 0, y: 0 }
        const current = node.name === this.selected
        return { name: node.name, label: GraphNode.label(node), where: where(node), x: at.x, y: at.y, current, near: near.has(node.name), faded: selected !== undefined && !current && !near.has(node.name) }
      }),
      edges: picture.edges.map((edge) => {
        const line = GraphLayout.edge(picture.placed.get(edge.from) ?? { x: 0, y: 0 }, picture.placed.get(edge.to) ?? { x: 0, y: 0 })
        return { ...line, hot: edge.from === this.selected || edge.to === this.selected }
      }),
      card,
    }
  }

  /** The file tree: every file the pictures name -- the Files picture's file nodes and the Functions picture's `file` fields -- by folder. */
  private tree(current: string | undefined): TreeRowFace[] {
    const paths = new Set<string>()
    for (const node of this.files?.nodes ?? []) if (node.type === 'file') paths.add(node.name)
    for (const node of this.functions?.nodes ?? []) if (node.file !== undefined) paths.add(node.file)
    const folders = new Map<string, string[]>()
    for (const path of [...paths].sort()) {
      const cut = path.lastIndexOf('/')
      const folder = cut < 0 ? '' : path.slice(0, cut + 1)
      folders.set(folder, [...(folders.get(folder) ?? []), path])
    }
    return [...folders.entries()].flatMap(([folder, files]) => [
      ...(folder === '' ? [] : [{ label: folder, indent: 8, path: '', current: false }]),
      ...files.map((path) => ({ label: path.slice(folder.length), indent: folder === '' ? 8 : 22, path, current: path === current })),
    ])
  }

  private proseFace(): ProseFace {
    const report = this.report
    const entity = this.entity
    const decision = (row: { readonly name: string; readonly status: string; readonly discipline: string; readonly decision: string }) => ({ text: row.decision, meta: `${row.name} · ${row.status} · ${row.discipline}` })
    return {
      shown: true,
      summary: report?.summary ?? '',
      communitiesTotal: report?.communitiesTotal ?? 0,
      communities: (report?.communities ?? []).map((community) => ({ summary: community.summary ?? '', members: community.members.map((member) => member.name) })),
      decisionsTotal: report?.decisionsTotal ?? 0,
      decisions: (report?.decisions ?? []).map(decision),
      hubs: (report?.hubs ?? []).map((hub) => ({ name: hub.node.name, meta: `${hub.node.type} · ${plural(hub.degree, 'relation')}` })),
      entity: entity
        ? {
            shown: true,
            kind: entity.node.type.toUpperCase(),
            name: entity.node.name,
            description: entity.description,
            degree: `${entity.degreeIn} in · ${entity.degreeOut} out`,
            communities: entity.communities.map((community) => `#${community.id} · ${plural(community.size, 'entity').replace('entitys', 'entities')}${community.summary === undefined ? '' : `: ${community.summary}`}`),
            decisions: entity.decisions.map(decision),
            relations: entity.relations.map((relation) => ({ heading: heading(relation), names: relation.neighbors.map((node) => node.name) })),
          }
        : noEntity,
    }
  }

  /** The save panel's face, while it is open. */
  savePanel(): SavePanelFace {
    const saving = this.saving
    if (!saving) return { shown: false, file: '', place: '', command: '', canSave: false }
    const file = saving.file.trim()
    return { shown: true, file: saving.file, place: 'In Documents', command: `apogee ${this.saveArguments(saving.kind, saving.graph, file === '' ? '…' : file).join(' ')}`, canSave: file !== '' }
  }

  /** The dedupe sheet's face, while it is open. */
  dedupeSheet(): DedupeSheetFace {
    const dedupe = this.dedupe
    const base = { shown: true, title: `Dedupe ${this.dedupeGraph}`, intro: '', note: '', printed: '', busy: '', failure: '', merges: false, done: false }
    switch (dedupe?.case) {
      case undefined:
        return { ...base, shown: false, title: '' }
      case 'planning':
        return { ...base, busy: `apogee graph dedupe ${this.dedupeGraph} --dry-run` }
      case 'planned': {
        const merges = this.dedupeMerges
        return {
          ...base,
          intro: 'The dry run -- nothing changed yet:',
          note: merges ? `apogee graph dedupe ${this.dedupeGraph} -- the entities named merge into the one each is kept as. Re-run Find communities after, as the CLI says.` : '',
          printed: dedupe.text,
          merges,
          done: !merges,
        }
      }
      case 'merging':
        return { ...base, busy: 'merging' }
      case 'merged':
        return { ...base, printed: dedupe.text, done: true }
      case 'failed':
        return { ...base, failure: dedupe.message, done: true }
    }
  }

  /** A confirm's words while it asks: Delete the graph, as the CLI says what it keeps; or a node's explain refused, OK alone. */
  confirmWords(kind: GraphConfirm): GraphConfirmWords | undefined {
    if (kind === 'explainRefused') return this.problem === undefined ? undefined : { title: "That didn't work", body: this.problem, cancel: '', confirm: 'OK', destructive: false }
    const name = this.pendingDelete
    if (name === undefined) return undefined
    const prose = this.stats.get(name)?.isProse ?? name === this.world?.prose
    return {
      title: `Delete the graph ${name}?`,
      body: prose
        ? `apogee graph delete ${name} -- the collection's graph is cleared; its chunks stay, and the next build starts from scratch.`
        : `apogee graph delete ${name} -- its database is removed; its config entry stays (apogee config delete-graph ${name} removes it), and Rebuild parses its sources again.`,
      cancel: 'Cancel',
      confirm: 'Delete the graph',
      destructive: true,
    }
  }
}
