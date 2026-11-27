import { describe, expect, test } from 'vitest'
import ingestUnregistrableErr from '@recorded/business/jobs/Fixtures/embed-ingest-unregistrable.err?raw'
import buildUnregistrableErr from '@recorded/business/jobs/Fixtures/graph-build-unregistrable.err?raw'
import buildUnregistrableOut from '@recorded/business/jobs/Fixtures/graph-build-unregistrable.out?raw'
import captureValidatedErr from '@recorded/business/jobs/Fixtures/knowledge-capture-validated.err?raw'
import captureValidatedOut from '@recorded/business/jobs/Fixtures/knowledge-capture-validated.out?raw'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { AppViewModel } from '../../../source/presentation/app/ViewModels/AppViewModel'

/** The app at `start`, still, set up and its start ticks played, each run's end landed. */
async function opened(start: string): Promise<AppViewModel> {
  const app = new AppViewModel(presetFor({ start, still: true, apogee: 'installed' }).preset, modeRules)
  await app.start()
  for (let round = 0; round < 12; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
  return app
}

describe("GUI M22's presets: a run that left a note, its recording played to its end in the still frame", () => {
  test('knowledge:ingested-with-a-note: my notes’ page, the toast stdout’s line, the CLI’s note on the said-card', async () => {
    const app = await opened('knowledge:ingested-with-a-note')
    expect(app.shell.route).toBe('knowledge')
    expect(app.knowledgeTab).toBe('collections')
    const face = app.collections.face()
    expect([face.page.name, face.run.shown]).toEqual(['my notes', false])
    expect(face.notes).toEqual({ shown: true, title: 'What the ingest said', lines: LineFramer.lines(ingestUnregistrableErr) })
    expect(app.shell.toast?.text).toBe('1 file(s), 1 chunk(s)')
    expect(app.child.unanswered).toEqual([])
    app.stop()
  })

  test('knowledge:captured-with-a-note: Records listed as its sandbox listed them, the toast the captured record, the verdict on the said-card', async () => {
    const app = await opened('knowledge:captured-with-a-note')
    expect(app.knowledgeTab).toBe('records')
    const face = app.records.face()
    expect(face.notes).toEqual({ shown: true, title: 'What the capture said', lines: LineFramer.lines(captureValidatedErr) })
    expect(face.rows.map((row) => row.decision)).toEqual(['Remove the cancel button.'])
    expect(app.shell.toast?.text).toBe(LineFramer.lines(captureValidatedOut)[0])
    expect(app.child.unanswered).toEqual([])
    app.stop()
  })

  test('graph:built-with-a-note: my notes’ prose graph, the toast its last line, the two notes on the said-card and never the narration', async () => {
    const app = await opened('graph:built-with-a-note')
    expect(app.shell.route).toBe('graph')
    const face = app.graph.face()
    expect(face.notes).toEqual({ shown: true, title: 'What the rebuild said', lines: LineFramer.lines(buildUnregistrableErr).filter((line) => line.startsWith('apogee graph: ')) })
    expect(face.notes.lines).toHaveLength(2)
    expect(app.shell.toast?.text).toBe(LineFramer.lines(buildUnregistrableOut).at(-1))
    expect(app.child.unanswered).toEqual([])
    app.stop()
  })
})
