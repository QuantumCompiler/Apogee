import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import suitesModelsList from '@recorded/business/reads/Fixtures/suites-models-list.json?raw'
import streaming from '../../../fixtures/data/protocol/chat-streaming-handmade.jsonl?raw'
import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { ModelKind, ModelRow } from '../../../source/business/reads/Models'
import { ApogeeClient } from '../../../source/business/session/ApogeeClient'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import { scenarioTurns } from '../../../source/presentation/app/ViewModels/AppViewModel'
import { streamingFrames } from '../../../source/presentation/session/Previews/SessionPresets'
import { SessionViewModel } from '../../../source/presentation/session/ViewModels/SessionViewModel'
import { SessionWindow } from '../../../source/presentation/session/Views/SessionWindow'

const settled = () => new Promise((resolve) => setTimeout(resolve, 0))

describe('the session window, drawn: what only the view decides (design 1v)', () => {
  test("a turn's recall is drawn above its reply -- above the activity it arrived after, as the old frame draws it", async () => {
    const child = new StandInChild({ rules: { chats: [{ arguments: ['chat'], answer: { stdout: streaming, freeze: streamingFrames.chat } }] }, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ start: 1, still: true })
    const model = new SessionViewModel(new ApogeeClient(child, clock), clock)
    const list = ReadDocument.from(suitesModelsList) as ReadDocument
    model.modelKinds = ModelKind.byBackend(list)
    model.modelRows = ModelRow.list(list)
    await model.start()
    model.attach('.')
    for (const text of scenarioTurns.streaming) model.writeAhead(text)
    clock.start()
    await settled()
    // The transcript keeps the CLI's order: the activity first, then the recall.
    const kinds = model.face().rows.map((row) => row.kind)
    expect(kinds.lastIndexOf('activity')).toBeLessThan(kinds.lastIndexOf('recalled'))
    const markup = renderToStaticMarkup(<SessionWindow model={model} />)
    const recall = markup.indexOf('recalled 1 earlier chat')
    const rewrite = markup.indexOf('utility — rewriting the follow-up into a search query with root')
    expect(recall).toBeGreaterThan(0)
    expect(rewrite).toBeGreaterThan(0)
    expect(recall).toBeLessThan(rewrite)
    clock.stop()
  })
})
