import graphProseExplain from '@recorded/business/reads/Fixtures/graph-prose-explain.json?raw'
import graphProseReport from '@recorded/business/reads/Fixtures/graph-prose-report.json?raw'
import graphProseStats from '@recorded/business/reads/Fixtures/graph-prose-stats.json?raw'
import graphStatsDeleted from '@recorded/business/reads/Fixtures/graph-stats-deleted.json?raw'
import explain from '@recorded/presentation/graph/Fixtures/explain.json?raw'
import query from '@recorded/presentation/graph/Fixtures/query.json?raw'
import stats from '@recorded/presentation/graph/Fixtures/stats.json?raw'
import { describe, expect, test } from 'vitest'
import { GraphExplain, GraphNode, GraphQuery, GraphReport, GraphStats } from '../../../source/business/reads/Graph'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const documentOf = (text: string) => ReadDocument.from(text) as ReadDocument

describe("the graph's reads (design 1o): the Mac app's GraphNode, GraphEdge, GraphQuery, GraphExplain, GraphStats and GraphReport (Graph.swift)", () => {
  test('graph query: its entities in order, each with its file and lines, and only the relations between them', () => {
    const picture = GraphQuery.read(documentOf(query))
    expect([picture.question, picture.match]).toEqual(['runner fetch', 'names'])
    expect(picture.nodes.map((node) => `${node.type} ${node.name}`)).toEqual([
      'class ingest.fetch.HttpClient',
      'class ingest.runner.Runner',
      'function ingest.runner.Runner._fetch',
      'file ingest/fetch.py',
      'function ingest.cli.main',
      'function ingest.runner.Runner.__init__',
      'function ingest.runner.Runner.run_batch',
      'file ingest/runner.py',
    ])
    expect(picture.nodes[6]).toEqual({ name: 'ingest.runner.Runner.run_batch', type: 'function', description: 'def run_batch(self, batch)', file: 'ingest/runner.py', line: 8, endLine: 10, hop: 1 })
    expect(picture.edges.filter((edge) => edge.relation === 'calls').map((edge) => `${edge.from} -> ${edge.to}`)).toEqual([
      'ingest.cli.main -> ingest.fetch.HttpClient',
      'ingest.cli.main -> ingest.runner.Runner',
      'ingest.runner.Runner.run_batch -> ingest.runner.Runner._fetch',
    ])
    expect(picture.edges.filter((edge) => edge.relation === 'imports')).toEqual([{ from: 'ingest/runner.py', to: 'ingest/fetch.py', relation: 'imports', origin: 'extracted' }])
    const stray = GraphQuery.read(documentOf('{"entities":[{"name":"a","type":"file"}],"relations":[{"from":"a","to":"b","relation":"imports"}]}'))
    expect(stray.edges, 'a relation to a node the document does not hold is left out').toEqual([])
  })

  test('a node’s place and its label: file:line; the name past the module its file names, a file by its path', () => {
    const nodes = GraphQuery.read(documentOf(query)).nodes
    const named = (name: string) => nodes.find((node) => node.name === name) as GraphNode
    expect(GraphNode.place(named('ingest.runner.Runner.run_batch'))).toBe('ingest/runner.py:8')
    expect(GraphNode.label(named('ingest.runner.Runner.run_batch'))).toBe('Runner.run_batch')
    expect(GraphNode.label(named('ingest.cli.main'))).toBe('main')
    expect(GraphNode.label(named('ingest.fetch.HttpClient'))).toBe('HttpClient')
    expect(GraphNode.label(named('ingest/runner.py'))).toBe('ingest/runner.py')
    expect(GraphNode.label({ name: 'json.dumps', type: 'name', description: '', file: undefined, line: undefined, endLine: undefined, hop: undefined })).toBe('json.dumps')
    expect(GraphNode.lines(named('ingest/runner.py'))).toBe(17)
  })

  test('graph stats: a named code graph’s counts and sources; a collection’s prose graph; a deleted graph, not built', () => {
    const code = GraphStats.read(documentOf(stats))
    expect([code.graph, code.built, code.nodes, code.edges, code.codeFiles, code.sources, code.kind, code.isProse]).toEqual(['demo', true, 16, 21, 4, ['/Users/someone/src'], 'named', false])
    const prose = GraphStats.read(documentOf(graphProseStats))
    expect([prose.graph, prose.built, prose.nodes, prose.edges, prose.communities, prose.mentions, prose.isProse]).toEqual(['notes', true, 4, 5, 1, 13, true])
    const deleted = GraphStats.read(documentOf(graphStatsDeleted))
    expect([deleted.graph, deleted.built, deleted.nodes, deleted.edges, deleted.codeFiles]).toEqual(['spare', false, 0, 0, 0])
  })

  test('graph explain: the node, its degree, its relations by relation and direction, its communities and decisions', () => {
    const runner = GraphExplain.read(documentOf(explain)) as GraphExplain
    expect([runner.node.name, runner.degreeIn, runner.degreeOut]).toEqual(['ingest.runner.Runner', 4, 1])
    expect(runner.relations.map((relation) => [relation.relation, relation.direction, relation.total, relation.neighbors.map((node) => node.name)])).toEqual([
      ['calls', 'in', 1, ['ingest.cli.main']],
      ['defined_in', 'out', 1, ['ingest/runner.py']],
      ['defined_in', 'in', 3, ['ingest.runner.Runner.__init__', 'ingest.runner.Runner._fetch', 'ingest.runner.Runner.run_batch']],
    ])
    const heat = GraphExplain.read(documentOf(graphProseExplain)) as GraphExplain
    expect([heat.node.name, heat.node.type, heat.description, heat.degreeIn, heat.degreeOut]).toEqual(['Heat pump', 'system', 'moves heat from a cold place to a warm one', 1, 2])
    expect(heat.communities).toEqual([{ id: 1, size: 3, summary: 'The heat pump, its compressor and its outdoor unit: one machine moving heat indoors.', members: [] }])
    expect(heat.decisions).toEqual([{ name: 'kr-20261021T050657Z-c74d1a', status: 'shipped', discipline: 'product', decision: 'Explain the outdoor unit in the heat pump guide. — Customers kept asking why the outdoor unit runs in winter.' }])
    expect(GraphExplain.read(documentOf('{"object":"graph.node"}'))).toBeUndefined()
  })

  test('graph report: the summary, the communities with their members, the decisions, and the busiest entities with their degree', () => {
    const report = GraphReport.read(documentOf(graphProseReport)) as GraphReport
    expect(report.summary).toBe('Graph "notes" holds 4 entities and 5 relations. Every relation was asserted by a model. Its busiest entity is Heat pump (system), with 3 relations. 1 community is stored; 1 has a summary. 1 decision record is attached.')
    expect([report.communitiesTotal, report.communities[0]?.members.map((member) => member.name)]).toEqual([1, ['Compressor', 'Heat pump', 'Outdoor unit']])
    expect([report.decisionsTotal, report.decisions[0]?.name, report.decisions[0]?.status, report.decisions[0]?.discipline]).toEqual([1, 'kr-20261021T050657Z-c74d1a', 'shipped', 'product'])
    expect(report.hubs.map((hub) => [hub.node.name, hub.node.type, hub.degree])).toEqual([
      ['Heat pump', 'system', 3],
      ['kr-20261021T050657Z-c74d1a', 'decision', 3],
      ['Compressor', 'component', 2],
      ['Outdoor unit', 'component', 2],
    ])
    expect(GraphReport.read(documentOf(stats)), 'a document that is no report').toBeUndefined()
  })
})
