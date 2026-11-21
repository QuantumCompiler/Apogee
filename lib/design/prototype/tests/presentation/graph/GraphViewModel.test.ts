import { describe, expect, test } from 'vitest'
import { overtakenNode } from '../../../source/presentation/graph/Previews/GraphWorlds'
import { callsSince, graphPlace, play, settle } from './GraphHarness'

async function shown(options: Parameters<typeof graphPlace>[0] = {}) {
  const place = graphPlace(options)
  await place.graph.appeared()
  return place
}

describe("the Code graph (design 1o): the Mac app's GraphViewModel over the recorded demo and notes graphs, drawn as the old prototype draws the place", () => {
  test('the Functions view: stats.json in the header, query.json’s code entities as cards, its calls as edges, Runner.run_batch open as the old gNode', async () => {
    const { graph, child } = await shown()
    expect(callsSince(child, 0)).toEqual(['reads graph stats demo', 'reads graph query --graph demo -- runner fetch', 'reads graph query --graph demo -- ingest/__init__.py ingest/cli.py ingest/fetch.py ingest/runner.py'])
    const face = graph.face()
    expect(face.header).toEqual({ line: '~/src · 4 files · 16 symbols' })
    expect(face.views.map((view) => [view.label, view.current])).toEqual([
      ['Functions', true],
      ['Files', false],
      ['Collection · notes', false],
    ])
    expect([face.code.shown, face.prose.shown, face.gone.shown, face.findCommunities]).toEqual([true, false, false, false])
    expect(face.code.nodes.map((node) => [node.label, node.where, node.current])).toEqual([
      ['HttpClient', 'ingest/fetch.py:1', false],
      ['Runner', 'ingest/runner.py:4', false],
      ['Runner._fetch', 'ingest/runner.py:12', false],
      ['main', 'ingest/cli.py:4', false],
      ['Runner.__init__', 'ingest/runner.py:5', false],
      ['Runner.run_batch', 'ingest/runner.py:8', true],
    ])
    expect(face.code.edges.map((edge) => edge.hot)).toEqual([false, false, true])
    expect(face.code.card).toEqual({
      shown: true,
      kind: 'FUNCTION',
      label: 'Runner.run_batch',
      where: 'ingest/runner.py:8',
      sig: 'def run_batch(self, batch)',
      inLabel: 'CALLED BY',
      outLabel: 'CALLS',
      callers: [],
      callees: [{ name: 'ingest.runner.Runner._fetch', label: 'Runner._fetch' }],
    })
    expect(face.code.tree).toEqual([
      { label: 'ingest/', indent: 8, path: '', current: false },
      { label: '__init__.py', indent: 22, path: 'ingest/__init__.py', current: false },
      { label: 'cli.py', indent: 22, path: 'ingest/cli.py', current: false },
      { label: 'fetch.py', indent: 22, path: 'ingest/fetch.py', current: false },
      { label: 'runner.py', indent: 22, path: 'ingest/runner.py', current: true },
    ])
  })

  test('selecting a node fills CALLED BY and CALLS from the recorded edges, its neighbours at full strength and the rest faded; a caller walks to it', async () => {
    const { graph, child } = await shown()
    const from = child.calls.length
    await graph.select('ingest.cli.main')
    expect(callsSince(child, from), 'a picture just read is the graph’s word: nothing asked').toEqual([])
    let face = graph.face()
    expect([face.code.card.callers, face.code.card.callees.map((callee) => callee.label)]).toEqual([[], ['HttpClient', 'Runner']])
    expect(face.code.nodes.map((node) => [node.label, node.near])).toEqual([
      ['HttpClient', true],
      ['Runner', true],
      ['Runner._fetch', false],
      ['main', false],
      ['Runner.__init__', false],
      ['Runner.run_batch', false],
    ])
    await graph.select('ingest.runner.Runner')
    face = graph.face()
    expect([face.code.card.kind, face.code.card.callers.map((caller) => caller.label), face.code.card.callees]).toEqual(['CLASS', ['main'], []])
    expect(face.code.tree.find((row) => row.current)?.path).toBe('ingest/runner.py')
  })

  test('Files switches the labels to IMPORTED BY and IMPORTS, over the hand-made files picture; the file tree picks a file’s first node', async () => {
    const { graph } = await shown()
    await graph.show('files')
    let face = graph.face()
    expect(face.views.map((view) => view.current)).toEqual([false, true, false])
    expect(face.code.nodes.map((node) => [node.label, node.where, node.current])).toEqual([
      ['ingest/__init__.py', '1 line', false],
      ['ingest/cli.py', '5 lines', false],
      ['ingest/fetch.py', '3 lines', false],
      ['ingest/runner.py', '17 lines', true],
    ])
    expect(face.code.card).toMatchObject({ kind: 'FILE', label: 'ingest/runner.py', where: 'ingest/runner.py · 17 lines', sig: 'Python file', inLabel: 'IMPORTED BY', outLabel: 'IMPORTS' })
    expect([face.code.card.callers.map((node) => node.label), face.code.card.callees.map((node) => node.label)]).toEqual([['ingest/cli.py'], ['ingest/fetch.py']])
    await graph.pickFile('ingest/cli.py')
    face = graph.face()
    expect([face.code.card.label, face.code.card.callees.map((node) => node.label)]).toEqual(['ingest/cli.py', ['ingest/fetch.py', 'ingest/runner.py']])
    await graph.show('functions')
    expect(graph.face().code.card.label, 'Functions opens on run_batch again, as the old showFns does').toBe('Runner.run_batch')
    await graph.pickFile('ingest/__init__.py')
    expect(graph.face().code.card.label, 'a file with no function in the picture picks nothing').toBe('Runner.run_batch')
    await graph.pickFile('ingest/cli.py')
    expect(graph.face().code.card.label).toBe('main')
  })

  test('the prose graph: graph-prose-stats.json’s line, graph-prose-report.json’s summary, community, decision and busiest, and its busiest entity explained', async () => {
    const { graph, child } = await shown()
    const from = child.calls.length
    await graph.show('prose')
    expect(callsSince(child, from)).toEqual(['reads graph stats notes', 'reads graph report --graph notes', 'reads graph explain --graph notes -- Heat pump'])
    const face = graph.face()
    expect([face.header.line, face.code.shown, face.prose.shown, face.findCommunities]).toEqual(['notes · 4 entities · 5 relations · 1 community', false, true, true])
    expect(face.prose).toEqual({
      shown: true,
      summary: 'Graph "notes" holds 4 entities and 5 relations. Every relation was asserted by a model. Its busiest entity is Heat pump (system), with 3 relations. 1 community is stored; 1 has a summary. 1 decision record is attached.',
      communitiesTotal: 1,
      communities: [{ summary: 'The heat pump, its compressor and its outdoor unit: one machine moving heat indoors.', members: ['Compressor', 'Heat pump', 'Outdoor unit'] }],
      decisionsTotal: 1,
      decisions: [{ text: 'Explain the outdoor unit in the heat pump guide. — Customers kept asking why the outdoor unit runs in winter.', meta: 'kr-20261021T050657Z-c74d1a · shipped · product' }],
      hubs: [
        { name: 'Heat pump', meta: 'system · 3 relations' },
        { name: 'kr-20261021T050657Z-c74d1a', meta: 'decision · 3 relations' },
        { name: 'Compressor', meta: 'component · 2 relations' },
        { name: 'Outdoor unit', meta: 'component · 2 relations' },
      ],
      entity: {
        shown: true,
        kind: 'SYSTEM',
        name: 'Heat pump',
        description: 'moves heat from a cold place to a warm one',
        degree: '1 in · 2 out',
        communities: ['#1 · 3 entities: The heat pump, its compressor and its outdoor unit: one machine moving heat indoors.'],
        decisions: [{ text: 'Explain the outdoor unit in the heat pump guide. — Customers kept asking why the outdoor unit runs in winter.', meta: 'kr-20261021T050657Z-c74d1a · shipped · product' }],
        relations: [
          { heading: 'CONCERNS (IN)', names: ['kr-20261021T050657Z-c74d1a'] },
          { heading: 'CONTAINS', names: ['Compressor', 'Outdoor unit'] },
        ],
      },
    })
  })

  test('the More menu: the old seven, a rule before Dedupe, Delete in the error color; opened through the shell’s scrim, closed by it', async () => {
    const { graph, menus } = await shown()
    graph.toggleMore()
    const more = graph.face().more
    expect([more.open, more.enabled]).toEqual([true, true])
    expect(more.items.map((item) => (item.rule ? '---' : `${item.label}${item.destructive ? ' !' : ''}`))).toEqual(['Export as HTML page…', 'Export as GraphML…', 'Export as Mermaid diagram…', 'Save the report…', '---', 'Dedupe…', 'Delete the graph… !'])
    graph.closeMore()
    expect([graph.face().more.open, menus]).toEqual([false, ['graphMore', undefined]])
  })

  test('the save panel offers the CLI’s own file name, refuses an empty one, and Save raises the recorded line with the chosen file name', async () => {
    const { graph, child, said, sheets } = await shown()
    graph.toggleMore()
    graph.exportAs('html')
    expect([graph.face().more.open, sheets]).toEqual([false, ['savePanel']])
    expect(graph.savePanel()).toEqual({ shown: true, file: 'demo.html', place: 'In Documents', command: 'apogee graph export html --graph demo -o /Users/someone/Documents/demo.html', canSave: true })
    graph.setSaveFile('  ')
    expect([graph.savePanel().canSave, graph.savePanel().command]).toEqual([false, 'apogee graph export html --graph demo -o /Users/someone/Documents/…'])
    graph.setSaveFile('demo.html')
    const from = child.calls.length
    await graph.save()
    expect(callsSince(child, from)).toEqual(['commands graph export html --graph demo -o /Users/someone/Documents/demo.html'])
    expect(said).toEqual(['Exported graph "demo" to /Users/someone/Documents/demo.html: 9 entities and 12 relations drawn, 41 KB.'])
    expect([graph.savePanel().shown, sheets]).toEqual([false, ['savePanel', 'dismissed']])
    graph.saveReport()
    expect(graph.savePanel().file).toBe('demo-report.md')
    await graph.save()
    expect(said.at(-1)).toBe('Wrote the report on graph "demo" to /Users/someone/Documents/demo-report.md (3 KB).')
    graph.exportAs('mermaid')
    graph.cancelSave()
    expect([graph.savePanel().shown, sheets.at(-1)]).toEqual([false, 'dismissed'])
  })

  test('dedupe on demo shows the dry run and Done only; on notes it offers Merge them, then merging, then the merged note', async () => {
    const { graph, child, clock, sheets } = await shown()
    await graph.planDedupe()
    expect(sheets).toEqual(['dedupe'])
    expect(graph.dedupeSheet()).toEqual({
      shown: true,
      title: 'Dedupe demo',
      intro: 'The dry run -- nothing changed yet:',
      note: '',
      printed: 'Code entities: 9 -- merged by exact qualified name as they were built (0 stated at more than one place, one node each); never by resemblance.\nProse entities: vector dedupe skipped -- no prose entities -- nothing to compare by vector; nothing to merge.',
      busy: '',
      failure: '',
      merges: false,
      done: true,
    })
    graph.dismissDedupe()
    await graph.show('prose')
    await graph.planDedupe()
    expect(graph.dedupeSheet()).toMatchObject({ title: 'Dedupe notes', note: 'apogee graph dedupe notes -- the entities named merge into the one each is kept as. Re-run Find communities after, as the CLI says.', merges: true, done: false })
    const from = child.calls.length
    graph.confirmDedupe()
    await settle()
    expect(callsSince(child, from)).toEqual(['jobs graph dedupe notes'])
    expect(graph.dedupeSheet()).toMatchObject({ intro: '', printed: '', busy: 'merging', merges: false, done: false })
    graph.dismissDedupe()
    expect(graph.dedupeSheet().shown, 'the sheet stays while it merges').toBe(true)
    await play(clock, 4)
    expect(graph.dedupeSheet()).toMatchObject({
      intro: '',
      printed: 'Merged into Compressor (component): compressor unit\n1 group(s), 1 entity(ies) merged at threshold 0.92.\nNote: community memberships changed -- re-run `apogee graph communities notes` to refresh the summaries.',
      busy: '',
      merges: false,
      done: true,
    })
  })

  test('delete asks with the CLI’s words, then shows the gone state, the More menu off; Rebuild brings the graph back, its line said', async () => {
    const { graph, child, clock, said, asked } = await shown()
    graph.askToDelete()
    expect(asked).toEqual(['graphDelete'])
    expect(graph.confirmWords('graphDelete')).toEqual({
      title: 'Delete the graph demo?',
      body: 'apogee graph delete demo -- its database is removed; its config entry stays (apogee config delete-graph demo removes it), and Rebuild parses its sources again.',
      cancel: 'Cancel',
      confirm: 'Delete the graph',
      destructive: true,
    })
    const from = child.calls.length
    await graph.confirmDelete()
    expect(callsSince(child, from), 'read back').toEqual(['commands graph delete demo', 'reads graph stats demo', 'reads graph query --graph demo -- runner fetch', 'reads graph query --graph demo -- ingest/__init__.py ingest/cli.py ingest/fetch.py ingest/runner.py'])
    expect(said).toEqual(['Deleted graph "spare": 10 node(s), 13 edge(s), 0 mention(s). The graphs: config entry is untouched (`apogee config delete-graph spare` removes it).'])
    let face = graph.face()
    expect([face.header.line, face.gone, face.code.shown, face.more.enabled]).toEqual(['0 nodes · 0 edges · 0 files', { shown: true, text: 'demo has no graph built. Rebuild parses its sources again -- no model, no key, no network.' }, false, false])
    graph.rebuild()
    await settle()
    expect(callsSince(child, from).at(-1)).toBe('jobs graph update demo')
    expect(graph.face().rebuild).toEqual({ enabled: false })
    await play(clock, 3)
    face = graph.face()
    expect([face.gone.shown, face.code.shown, face.header.line, face.more.enabled, face.rebuild.enabled]).toEqual([false, true, '~/src · 4 files · 16 symbols', true, true])
    expect(said.at(-1), 'graph.cpp’s Code graph line, its sync suffix a rebuild of the deleted graph’s').toBe('  Code graph:       16 node(s), 21 edge(s) (nodes +16 ~0 -0; edges +21 -0)')
  })

  test('the prose graph’s delete says what a collection’s delete keeps, and clears it as the CLI says', async () => {
    const { graph, said } = await shown()
    await graph.show('prose')
    graph.askToDelete()
    expect(graph.confirmWords('graphDelete')?.body).toBe("apogee graph delete notes -- the collection's graph is cleared; its chunks stay, and the next build starts from scratch.")
    graph.dismissDelete()
    expect(graph.confirmWords('graphDelete')).toBeUndefined()
    graph.askToDelete()
    await graph.confirmDelete()
    expect([said, graph.face().gone.text, graph.face().prose.shown]).toEqual([['Deleted the graph for "notes": 4 node(s), 5 edge(s), 13 mention(s). The chunks are untouched; the next build starts from scratch.'], 'notes has no graph built. Rebuild parses its sources again -- no model, no key, no network.', false])
    await graph.show('functions')
    expect(graph.face().gone.shown, 'demo is still built').toBe(false)
  })

  test('a picture an update elsewhere overtook: Runner._fetch clicked, its explain refused in the CLI’s words over the kept canvas, no card; OK clears the selection', async () => {
    const { graph, child, asked } = await shown()
    graph.overtaken()
    const from = child.calls.length
    await graph.select(overtakenNode)
    expect(callsSince(child, from)).toEqual([`reads graph explain --graph demo -- ${overtakenNode}`])
    expect(asked).toEqual(['explainRefused'])
    expect(graph.confirmWords('explainRefused')).toEqual({
      title: "That didn't work",
      body: "apogee graph: no node named 'ingest.runner.Runner._fetch' in graph 'demo' -- near matches: function:ingest.runner.Runner._fetch_one (ingest/runner.py:12), class:ingest.runner.Runner (ingest/runner.py:4), file:ingest/runner.py (ingest/runner.py:1), function:ingest.runner.Runner.run_batch (ingest/runner.py:8), function:ingest.runner.Runner.__init__ (ingest/runner.py:5) (exited with status 1)",
      cancel: '',
      confirm: 'OK',
      destructive: false,
    })
    let face = graph.face()
    expect([face.code.shown, face.code.card.shown, face.code.nodes.find((node) => node.current)?.label]).toEqual([true, false, 'Runner._fetch'])
    graph.dismissProblem()
    face = graph.face()
    expect([face.code.nodes.some((node) => node.current), face.code.card.shown, face.code.nodes.length, graph.confirmWords('explainRefused')]).toEqual([false, false, 6, undefined])
    await graph.select('ingest.runner.Runner')
    expect([graph.face().code.card.label, asked]).toEqual(['Runner', ['explainRefused']])
  })

  test('in an overtaken picture only Runner._fetch is refused, as the old pickNode refuses it alone: after OK every other click, in Functions and in Files, shows its card', async () => {
    const { graph, child, asked } = await shown()
    graph.overtaken()
    await graph.select(overtakenNode)
    graph.dismissProblem()
    const from = child.calls.length
    await graph.select('ingest.runner.Runner.run_batch')
    expect([graph.face().code.card.shown, graph.face().code.card.label]).toEqual([true, 'Runner.run_batch'])
    await graph.select('ingest.cli.main')
    expect([graph.face().code.card.shown, graph.face().code.card.label]).toEqual([true, 'main'])
    await graph.show('files')
    await graph.select('ingest/cli.py')
    expect([graph.face().code.card.shown, graph.face().code.card.label]).toEqual([true, 'ingest/cli.py'])
    await graph.pickFile('ingest/fetch.py')
    expect([graph.face().code.card.shown, graph.face().code.card.label]).toEqual([true, 'ingest/fetch.py'])
    expect(asked).toEqual(['explainRefused'])
    expect(callsSince(child, from).filter((call) => call.startsWith('reads graph explain'))).toEqual([
      'reads graph explain --graph demo -- ingest.runner.Runner.run_batch',
      'reads graph explain --graph demo -- ingest.cli.main',
      'reads graph explain --graph demo -- ingest/cli.py',
      'reads graph explain --graph demo -- ingest/fetch.py',
    ])
    expect(child.unanswered).toEqual([])
  })

  test('a node the picture does not draw is explained of nobody: with the graph unread, choosing one asks nothing and raises nothing', async () => {
    const { graph, child, asked } = await shown({ apogee: 'config-typo' })
    graph.overtaken()
    const from = child.calls.length
    await graph.select(overtakenNode)
    expect([callsSince(child, from), asked, graph.face().code.card.shown]).toEqual([[], [], false])
  })

  test('Ask about this hands the composition the file and the old draft; Open in editor and Find communities stay the old toasts', async () => {
    const { graph, askedAbout, said } = await shown()
    graph.askAboutThis()
    graph.openInEditor()
    await graph.show('files')
    graph.askAboutThis()
    graph.openInEditor()
    graph.findCommunities()
    expect(askedAbout).toEqual([
      ['/Users/someone/src/ingest/runner.py', 'Explain Runner.run_batch and what depends on it.'],
      ['/Users/someone/src/ingest/runner.py', 'Explain ingest/runner.py and what depends on it.'],
    ])
    expect(said).toEqual(['Opened ingest/runner.py:8 in your editor (prototype)', 'Opened ingest/runner.py in your editor (prototype)', 'apogee graph communities notes: the utility model summarises each group (prototype)'])
  })

  test('no apogee: no graph and no words where the canvas stands, More off, as the old has no missing state (the Mac’s line a follow-up’s); a config typo: the CLI’s refusal', async () => {
    const missing = await shown({ apogee: 'missing' })
    expect([missing.graph.face().gone, missing.graph.face().code.shown, missing.graph.face().header.line, missing.graph.face().more.enabled, missing.graph.face().findCommunities]).toEqual([{ shown: true, text: '' }, false, '', false, false])
    const typo = await shown({ apogee: 'config-typo' })
    expect(typo.graph.face().gone.text).toBe("apogee graph: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
  })
})
