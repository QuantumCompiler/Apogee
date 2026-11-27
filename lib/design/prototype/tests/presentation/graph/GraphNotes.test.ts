import { describe, expect, test } from 'vitest'
import buildUnregistrableErr from '@recorded/business/jobs/Fixtures/graph-build-unregistrable.err?raw'
import buildUnregistrableOut from '@recorded/business/jobs/Fixtures/graph-build-unregistrable.out?raw'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { graphNotedRules, graphPlaceRules, graphWorld, notedGraph } from '../../../source/presentation/graph/Previews/GraphWorlds'
import { callsSince, graphPlace, play } from './GraphHarness'

/** The place over its answers and the noted sandbox's, the world's prose graph `my notes`, as the noted preset sets it up. */
async function noted() {
  const place = graphPlaceRules()
  const notes = graphNotedRules()
  const harness = graphPlace({ rules: { reads: [...place.reads, ...notes.reads], commands: place.commands, jobs: [...place.jobs, ...notes.jobs] } })
  harness.graph.useWorld({ ...graphWorld, prose: notedGraph })
  await harness.graph.show('prose')
  return harness
}

describe("a rebuild's notes on the said-card, the Mac's GUI M22 (design)", () => {
  test('my notes rebuilt: the toast is its last line, and the card keeps the two notes word for word, never the narration', async () => {
    const { graph, child, clock, said } = await noted()
    const from = child.calls.length
    graph.rebuild()
    await play(clock, LineFramer.lines(buildUnregistrableOut).length + 2)
    expect(callsSince(child, from)[0]).toBe(`jobs graph build ${notedGraph}`)
    expect(said.at(-1)).toBe(LineFramer.lines(buildUnregistrableOut).at(-1))
    expect(graph.face().notes).toEqual({ shown: true, title: 'What the rebuild said', lines: LineFramer.lines(buildUnregistrableErr).slice(1) })
  })

  test('another graph shown drops the card; Dismiss drops it too', async () => {
    const { graph, clock } = await noted()
    graph.rebuild()
    await play(clock, LineFramer.lines(buildUnregistrableOut).length + 2)
    expect(graph.face().notes.shown).toBe(true)
    await graph.show('functions')
    expect(graph.face().notes.shown).toBe(false)
    await graph.show('prose')
    graph.rebuild()
    await play(clock, LineFramer.lines(buildUnregistrableOut).length + 2)
    graph.dismissNotes()
    expect(graph.face().notes.shown).toBe(false)
  })

  test('a rebuild that said nothing on stderr leaves no card: the demo graph’s update', async () => {
    const { graph, clock } = graphPlace()
    await graph.appeared()
    graph.rebuild()
    await play(clock, 12)
    expect(graph.face().notes).toEqual({ shown: false, title: '', lines: [] })
  })
})
