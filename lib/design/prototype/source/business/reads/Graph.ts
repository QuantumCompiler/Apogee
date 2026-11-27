import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/**
 * The graph's reads (design 1o): the Mac app's `GraphNode`, `GraphEdge`,
 * `GraphQuery`, `GraphExplain`, `GraphStats`, `GraphCommunity`,
 * `GraphDecision` and `GraphReport` (`BusinessReads`, `Graph.swift`), each
 * field as the CLI's `graph query | explain | stats | report` documents carry
 * it.
 */

const text = (value: JSONValue | undefined): string => JSONValue.string(value) ?? ''

/** A graph's node, as the graph documents name it. */
export interface GraphNode {
  readonly name: string
  /** `class`, `function`, `file`, `name`, or a prose graph's `system`, `component`, `decision` … */
  readonly type: string
  readonly description: string
  readonly file: string | undefined
  readonly line: number | undefined
  readonly endLine: number | undefined
  /** How far a query walked to it: 0 for what the question named. */
  readonly hop: number | undefined
}

export const GraphNode = {
  read(value: JSONValue): GraphNode {
    const row = JSONValue.object(value)
    return {
      name: text(row?.name),
      type: text(row?.type),
      description: text(row?.description),
      file: JSONValue.string(row?.file),
      line: JSONValue.int(row?.line),
      endLine: JSONValue.int(row?.end_line),
      hop: JSONValue.int(row?.hop),
    }
  },

  /** Where it is written, `file:line`: the Mac's `place`. */
  place(node: GraphNode): string | undefined {
    return node.file === undefined ? undefined : `${node.file}${node.line === undefined ? '' : `:${node.line}`}`
  },

  /** How many lines it spans, from its first to its last: a file's length. */
  lines(node: GraphNode): number | undefined {
    return node.line === undefined || node.endLine === undefined ? undefined : node.endLine - node.line + 1
  },

  /**
   * Its name as its own file reads it: past the module its file names
   * (`ingest/runner.py` is `ingest.runner`, so `ingest.runner.Runner.run_batch`
   * is `Runner.run_batch`, as the old prototype's card names it); a file by
   * its path, and a name in no file whole.
   */
  label(node: GraphNode): string {
    if (node.type === 'file' || node.file === undefined) return node.name
    const module = node.file.replace(/\.[^./]+$/, '').replace(/\//g, '.').replace(/\.__init__$/, '')
    return node.name.startsWith(`${module}.`) ? node.name.slice(module.length + 1) : node.name
  },
}

/** An edge between two nodes. */
export interface GraphEdge {
  readonly from: string
  readonly to: string
  readonly relation: string
  /** `extracted` or `inferred`. */
  readonly origin: string
}

/** `graph query <question> --output-format json`: the entities the question names and the bounded neighbourhood around them. */
export interface GraphQuery {
  readonly question: string
  readonly nodes: readonly GraphNode[]
  /** Only the relations between nodes the document holds. */
  readonly edges: readonly GraphEdge[]
  /** `names`, `words` … how the question matched. */
  readonly match: string
}

export const GraphQuery = {
  read(document: ReadDocument): GraphQuery {
    const value = document.value
    const nodes = (JSONValue.array(value.entities) ?? []).map(GraphNode.read).filter((node) => node.name !== '')
    const names = new Set(nodes.map((node) => node.name))
    const edges = (JSONValue.array(value.relations) ?? []).flatMap((entry) => {
      const relation = JSONValue.object(entry)
      const from = JSONValue.string(relation?.from)
      const to = JSONValue.string(relation?.to)
      if (from === undefined || to === undefined || !names.has(from) || !names.has(to)) return []
      return [{ from, to, relation: text(relation?.relation), origin: text(relation?.origin) }]
    })
    return { question: text(value.question), nodes, edges, match: text(value.match) }
  },
}

/** One relation of an explained node: its neighbours that way, and how many there are in all. */
export interface GraphRelation {
  readonly relation: string
  /** `in` or `out`. */
  readonly direction: string
  readonly neighbors: readonly GraphNode[]
  readonly total: number
}

/** A community of a graph: its members' count, and the summary a model wrote for it when one is stored. */
export interface GraphCommunity {
  readonly id: number
  readonly size: number
  readonly summary: string | undefined
  /** Its members, where the document names them (the report does). */
  readonly members: readonly GraphNode[]
}

const community = (value: JSONValue): GraphCommunity[] => {
  const row = JSONValue.object(value)
  const id = JSONValue.int(row?.id)
  if (id === undefined) return []
  const summary = JSONValue.string(row?.summary)
  return [{ id, size: JSONValue.int(row?.size) ?? 0, summary: summary === undefined || summary === '' ? undefined : summary, members: (JSONValue.array(row?.members) ?? []).map(GraphNode.read) }]
}

/** A decision record attached to a graph: its node's name (the record's id), its branch and discipline, and the decision in the CLI's words. */
export interface GraphDecision {
  readonly name: string
  readonly status: string
  readonly discipline: string
  readonly decision: string
}

/** From an explain's decision (the node's own fields) or a report's (the node under `node`). */
const decision = (value: JSONValue): GraphDecision[] => {
  const row = JSONValue.object(value)
  const node = JSONValue.object(row?.node) ?? row
  const name = JSONValue.string(node?.name)
  if (name === undefined) return []
  return [{ name, status: text(node?.status), discipline: text(node?.discipline), decision: text(row?.decision) }]
}

/** `graph explain <node> --output-format json`: everything around one node. */
export interface GraphExplain {
  readonly node: GraphNode
  readonly description: string
  readonly degreeIn: number
  readonly degreeOut: number
  readonly relations: readonly GraphRelation[]
  readonly communities: readonly GraphCommunity[]
  readonly decisions: readonly GraphDecision[]
}

export const GraphExplain = {
  /** The node explained; `undefined` for a document with no named node. */
  read(document: ReadDocument): GraphExplain | undefined {
    const value = document.value
    const node = JSONValue.object(value.node)
    if (JSONValue.string(node?.name) === undefined || node === undefined) return undefined
    const degree = JSONValue.object(value.degree)
    return {
      node: GraphNode.read(node),
      description: text(value.description),
      degreeIn: JSONValue.int(degree?.in) ?? 0,
      degreeOut: JSONValue.int(degree?.out) ?? 0,
      relations: (JSONValue.array(value.relations) ?? []).map((entry) => {
        const relation = JSONValue.object(entry)
        return { relation: text(relation?.relation), direction: text(relation?.direction), neighbors: (JSONValue.array(relation?.neighbors) ?? []).map(GraphNode.read), total: JSONValue.int(relation?.total) ?? 0 }
      }),
      communities: (JSONValue.array(value.communities) ?? []).flatMap(community),
      decisions: (JSONValue.array(value.decisions) ?? []).flatMap(decision),
    }
  },
}

/** `graph stats <name> --output-format json`: a graph's counts. */
export interface GraphStats {
  readonly graph: string
  readonly built: boolean
  readonly nodes: number
  readonly edges: number
  readonly codeFiles: number
  readonly mentions: number
  readonly staleFiles: number
  readonly sources: readonly string[]
  /** `collection` for a collection's own (prose) graph, else the named graph's kind. */
  readonly kind: string
  readonly communities: number
  /** A collection's own graph: prose, its entities drawn by a model. */
  readonly isProse: boolean
}

export const GraphStats = {
  read(document: ReadDocument): GraphStats {
    const value = document.value
    const kind = text(value.kind)
    return {
      graph: text(value.graph),
      built: JSONValue.bool(value.built) ?? false,
      nodes: JSONValue.int(value.nodes) ?? 0,
      edges: JSONValue.int(value.edges) ?? 0,
      codeFiles: JSONValue.int(value.code_files) ?? 0,
      mentions: JSONValue.int(value.mentions) ?? 0,
      staleFiles: JSONValue.int(value.stale_files) ?? 0,
      sources: (JSONValue.array(value.sources) ?? []).flatMap((source) => (typeof source === 'string' ? [source] : [])),
      kind,
      communities: JSONValue.int(value.communities) ?? 0,
      isProse: kind === 'collection',
    }
  },
}

/** One of the busiest entities, with its relation count. */
export interface GraphHub {
  readonly node: GraphNode
  readonly degree: number
}

/** `graph report --output-format json`: the graph's own summary -- its communities, the decisions attached, its busiest entities -- from the store, with no model call. */
export interface GraphReport {
  readonly summary: string
  readonly communities: readonly GraphCommunity[]
  readonly communitiesTotal: number
  readonly decisions: readonly GraphDecision[]
  readonly decisionsTotal: number
  readonly hubs: readonly GraphHub[]
}

export const GraphReport = {
  /** The report; `undefined` for a document that is no `graph.report`. */
  read(document: ReadDocument): GraphReport | undefined {
    const value = document.value
    if (document.object !== 'graph.report') return undefined
    const part = (key: string) => JSONValue.object(value[key])
    return {
      summary: text(value.human_summary),
      communities: (JSONValue.array(part('communities')?.shown) ?? []).flatMap(community),
      communitiesTotal: JSONValue.int(part('communities')?.total) ?? 0,
      decisions: (JSONValue.array(part('decisions')?.shown) ?? []).flatMap(decision),
      decisionsTotal: JSONValue.int(part('decisions')?.total) ?? 0,
      hubs: (JSONValue.array(part('hubs')?.shown) ?? []).flatMap((entry) => {
        const hub = JSONValue.object(entry)
        const node = hub?.node
        if (node === undefined) return []
        return [{ node: GraphNode.read(node), degree: JSONValue.int(JSONValue.object(hub?.degree)?.total) ?? 0 }]
      }),
    }
  },
}
