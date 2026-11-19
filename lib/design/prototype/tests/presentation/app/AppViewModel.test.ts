import { describe, expect, test } from 'vitest'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel, scenarioTurns } from '../../../source/presentation/app/ViewModels/AppViewModel'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'
import { sessionSamples } from '../../../source/presentation/session/Previews/SessionPresets'

/** Lets every read the start began land. */
const settled = () => new Promise((resolve) => setTimeout(resolve, 0))

/** The app composed at `start`, still, in `apogee`'s mode, and started. */
async function started(start: string | undefined, apogee: ApogeeMode = 'installed') {
  const app = new AppViewModel(presetFor({ ...(start ? { start } : {}), still: true, apogee }).preset, modeRules, undefined, sessionSamples)
  await app.start()
  await settled()
  return app
}

describe('the composition root', () => {
  test('it is named Apogee, and builds the stand-in, the clock and every view-model once, from the preset', () => {
    const preset = presetFor({ start: 'monitor', still: true, apogee: 'installed' }).preset
    const app = new AppViewModel(preset, modeRules)
    expect(app.name).toBe('Apogee')
    expect(app.preset).toBe(preset)
    expect(app.clock.still).toBe(true)
    expect(app.child.apogee).toBe('installed')
    expect(app.shell.theme).toBe(app.theme)
  })

  test("the monitor preset opens the monitor over Sessions with the system strip open, as the old applyStart('monitor') does", async () => {
    const app = await started('monitor')
    expect(app.shell.route).toBe('sessions')
    expect(app.shell.overlay).toBe('monitor')
    expect(app.shell.systemOpen).toBe(true)
    expect(app.monitor.isOpen).toBe(true)
  })

  test("its numbers are the recorded system document's, after a short run of readings with one failed read", async () => {
    const app = await started('monitor')
    expect(app.child.calls.filter((call) => call.arguments.join(' ') === 'system')).toHaveLength(4)
    expect(app.monitor.reading?.cpuPercent).toBe(2.36)
    expect(app.monitor.reading?.memoryUsed).toBe(27_916_550_144)
    expect(app.monitor.reading?.storeBytes).toBe(257_698_037_868)
    expect(app.monitor.history).toEqual([2.36, 2.36, 2.36])
    expect(app.monitor.face().gpu.stale, 'the frame after the failed read is fresh again').toBeUndefined()
    expect(app.strip).toEqual(app.monitor.strip)
    expect(app.child.unanswered).toEqual([])
  })

  test('opening the file with no start shows chat, the old data-props default: the Streaming session mid-reasoning on Sessions (1v)', async () => {
    const app = await started(undefined)
    expect(app.preset.route).toBe('chat')
    expect(app.shell.route).toBe('sessions')
    expect(app.shell.overlay).toBeUndefined()
    expect(app.session.transcript.status.case).toBe('thinking')
  })

  test("the monitor's \"Open models library\" goes to Models' store tab and closes the monitor, as the old one does", async () => {
    const app = await started('monitor')
    app.monitor.showLibrary()
    expect(app.shell.route).toBe('models')
    expect(app.shell.overlay).toBeUndefined()
    expect(app.models.tab).toBe('store')
  })

  test('Unload on a monitor row raises the toast the old prototype raises, until a place fills it', async () => {
    const app = await started('monitor')
    app.monitor.unloadModel('root')
    expect(app.shell.toast?.text).toBe('Unloaded root')
  })

  test("each route change calls the place's appeared()", async () => {
    const app = await started('monitor')
    expect(app.models.appearances).toBe(0)
    app.shell.go('models')
    app.shell.go('tasks')
    app.shell.go('models')
    expect(app.models.appearances).toBe(2)
  })

  test('the monitor reads while it or the strip is open, and stops when both close', async () => {
    const app = await started('monitor')
    app.shell.closeOverlay()
    expect(app.monitor.isOpen, 'the strip still shows its numbers').toBe(true)
    app.shell.setSystemOpen(false)
    expect(app.monitor.isOpen).toBe(false)
    app.shell.toggleSystem()
    expect(app.monitor.isOpen).toBe(true)
  })

  test('missing: no binary is found, so the monitor says so in the Mac app\'s words', async () => {
    const app = await started('monitor', 'missing')
    expect(app.monitor.failure).toBe('Apogee isn’t installed, so the system can’t be read.')
    expect(app.child.calls).toEqual([])
  })

  test("starting twice sets the preset up once: a monitor the person closed stays closed (React's StrictMode starts an effect twice)", async () => {
    const app = await started('monitor')
    app.shell.closeOverlay()
    app.shell.setSystemOpen(false)
    app.stop()
    await app.start()
    await settled()
    expect(app.shell.overlay).toBeUndefined()
    expect(app.shell.systemOpen).toBe(false)
    expect(app.child.calls.filter((call) => call.arguments[0] === 'system')).toHaveLength(4)
    expect(app.child.calls.filter((call) => call.kind === 'chats'), 'its session opened once').toHaveLength(1)
  })
})

describe('the preset table', () => {
  test('monitor is known, and an unknown preset opens the default, saying it was not known', () => {
    expect(presetFor({ start: 'monitor', still: true, apogee: 'installed' }).known).toBe(true)
    const unknown = presetFor({ start: 'no-such-screen', still: false, apogee: 'missing' })
    expect(unknown.known).toBe(false)
    expect(unknown.preset.route).toBe('chat')
    expect(unknown.preset.apogee).toBe('missing')
    expect(unknown.preset.clock.still).toBe(false)
  })
})

describe('the session in the composition (design 1u)', () => {
  test("under the monitor, Sessions is the Streaming session at chat's frame, launched (1v; the old progress 30)", async () => {
    const app = await started('monitor')
    const chat = await started('chat')
    expect(app.shell.overlay).toBe('monitor')
    expect(app.session.connection.case).toBe('connected')
    expect(app.session.face().rows).toEqual(chat.session.face().rows)
    expect(app.session.transcript.status.case).toBe('thinking')
    expect(app.child.unanswered).toEqual([])
  })

  test('chat:new opens a new chat on Sessions: its empty state, its model from models list and models info, the inspector open', async () => {
    const app = await started('chat:new')
    expect(app.shell.route).toBe('sessions')
    expect(app.shell.overlay).toBeUndefined()
    const face = app.session.face()
    expect(face.state).toBe('empty')
    expect(face.header).toMatchObject({ title: 'New session', inspectorOpen: true, model: { name: 'claude', kind: 'api', tag: 'API · billed per use' } })
    expect(face.header.meter.text, 'models info states no window for claude').toBe('unknown')
    expect(app.child.calls.map((call) => `${call.kind}: ${call.arguments.join(' ')}`)).toEqual([
      'chats: chat --output-format stream-json --input-format stream-json --tools',
      'reads: models list',
      'commands: config get models.default_suite',
      'reads: models info claude',
    ])
  })

  test('chat:private: the private empty state and the CLI’s notice', async () => {
    const app = await started('chat:private')
    const face = app.session.face()
    expect(face.header.privateBadge).toBe(true)
    expect(face.empty.heading).toBe('Private session')
    expect(face.empty.notices).toEqual(['private: nothing of this chat is written -- no transcript, no recall'])
    expect(face.composer.placeholder).toBe('Message claude · private')
  })

  for (const preset of ['chat:new', 'chat:private']) {
    test(`${preset}: the header names the suite a new chat runs under, read when the session opens (the CLI's recorded default)`, async () => {
      const app = await started(preset)
      expect(app.session.face().header).toMatchObject({ suiteWords: 'suite fast, the default', canSwitchSuite: true })
      expect(app.child.unanswered).toEqual([])
    })
  }

  for (const preset of ['chat:new', 'chat:private', 'chat:play']) {
    test(`${preset}: Choose a suite (or /suite) opens the chooser over the recorded suites, every read answered`, async () => {
      const app = await started(preset)
      app.session.chooseSuite()
      await settled()
      expect(app.child.unanswered).toEqual([])
      expect(app.shell.sheet).toBe('suite')
      expect(app.session.face().suiteSheet.rows.map((row) => row.name)).toContain('vast')
    })
  }

  test('under the monitor the Streaming session’s acts reach the CLI: the suite chooser opens over the recorded suites, and an attach mid-turn is held for its end', async () => {
    const app = await started('monitor')
    const session = app.session
    expect(session.face().header.canSwitchSuite).toBe(true)
    session.chooseSuite()
    session.attach('notes.md')
    await settled()
    expect(app.child.unanswered).toEqual([])
    expect(app.shell.sheet).toBe('suite')
    expect(session.held).toEqual([{ case: 'attach', path: 'notes.md' }])
    expect(session.face().held.words).toBe('Sent when this turn ends: attach notes.md')
  })

  test('chat:play: the captured execute session, its play sent before the clock started, the frame its finished turn', async () => {
    const app = await started('chat:play')
    const face = app.session.face()
    expect(face.state).toBe('conversation')
    expect(face.header.banner).toBe('execute · suite coding · orchestrating · in ingest')
    expect(face.header.model).toMatchObject({ name: 'root', kind: 'local', tag: 'local · this Mac' })
    expect(face.header.meter).toMatchObject({ known: false, text: 'not reported' })
    expect(face.rows.map((row) => row.kind)).toEqual(['notice', 'notice', 'notice', 'notice', 'user', 'notice', 'notice', 'activity', 'answer'])
    expect(face.rows[4]).toMatchObject({ kind: 'user', text: '/play summarize-verify The board met on 14 October. It agreed to move the release to 3 November, and Ana Ruiz will own the checklist.' })
    expect(face.rows[7]).toMatchObject({ kind: 'activity', summary: 'played summarize-verify · 2 stages', lines: [{ mark: '1/2' }, { mark: '2/2' }] })
    expect(face.rows[8]).toMatchObject({ kind: 'answer', text: 'The board met on 14 October and moved the release to 3 November; Ana Ruiz owns the checklist.', finished: true, footer: { model: 'root', kind: 'local' } })
    expect(app.session.plays.map((play) => play.name)).toContain('summarize-verify')
    expect(face.pending.shown).toBe(false)
    expect(face.composer.streaming).toBe(false)
  })

  test('chat:suite: a new chat with the chooser open on vast, over the recorded suites and the CLI’s verdict on each', async () => {
    const app = await started('chat:suite')
    expect(app.shell.sheet).toBe('suite')
    const sheet = app.session.face().suiteSheet
    expect(sheet.open).toBe(true)
    expect(sheet.now).toBe('Now: suite fast, the default')
    expect(sheet.rows.map((row) => [row.title, row.verdict, row.chosen])).toEqual([
      ['cloud', 'fits', false],
      ['fast (the default)', 'no member holds memory on this machine', false],
      ['pair', 'no member holds memory on this machine', false],
      ['shared', 'fits', false],
      ['vast', 'it does not fit', true],
      ['No suite', 'the roles follow the global pointers', false],
    ])
    expect(sheet).toMatchObject({ overBudget: true, force: false, canSwitch: false })
    expect(sheet.total).toBe("suite vast needs 247056 MiB of this machine's 110100 MiB: root 41096 + vast 204936 + 1024 margin -- it does not fit")
    app.closeSuites()
    expect(app.shell.sheet).toBeUndefined()
    expect(app.session.suiteChooser).toBeUndefined()
  })

  test('“Private session instead” on chat:new opens a private session in its place', async () => {
    const app = await started('chat:new')
    const before = app.session
    before.leave({ type: 'private' })
    await settled()
    expect(app.session).not.toBe(before)
    expect(app.session.privateMode).toBe('nothingWritten')
    expect(app.session.face().empty.heading).toBe('Private session')
    expect(app.child.unanswered).toEqual([])
  })

  test('“Private session instead” on chat:suite, the chooser cancelled, replays the private recording: the CLI’s private notice under the empty state', async () => {
    const app = await started('chat:suite')
    app.closeSuites()
    app.session.leave({ type: 'private' })
    await settled()
    expect(app.session.transcript.isPrivate).toBe(true)
    expect(app.session.face().empty.notices).toEqual(['private: nothing of this chat is written -- no transcript, no recall'])
    expect(app.child.unanswered).toEqual([])
  })

  test('the app root’s leave opens the session on Sessions, from another place, the monitor closed (as the old sessionGo)', async () => {
    const app = await started('chat:new')
    app.shell.go('models')
    app.leave({ type: 'new', suite: 'fast' })
    await settled()
    expect(app.shell.route).toBe('sessions')
    expect(app.session.launchSettings).toEqual({ suite: 'fast', forceSuite: false })
    expect(app.child.calls.filter((call) => call.kind === 'chats').at(-1)?.arguments.slice(-2)).toEqual(['--suite', 'fast'])
    expect(app.child.unanswered).toEqual([])
    app.shell.go('suites')
    app.shell.open('monitor')
    app.leave({ type: 'private' })
    await settled()
    expect(app.shell.route).toBe('sessions')
    expect(app.shell.overlay).toBeUndefined()
    expect(app.session.privateMode).toBe('nothingWritten')
  })

  test('a private leave stays put while the endPrivate confirm asks, and goes to Sessions once End it and switch is pressed', async () => {
    const app = await started('chat:private')
    app.session.setDraft('a secret')
    app.session.submit()
    app.shell.go('models')
    app.leave({ type: 'new' })
    expect(app.shell.confirm).toBe('endPrivate')
    expect(app.shell.route).toBe('models')
    const before = app.session
    app.endPrivateAndLeave()
    await settled()
    expect(app.shell.route).toBe('sessions')
    expect(app.session).not.toBe(before)
    expect(app.session.privateMode).toBeUndefined()
  })

  test('the session’s acts reach the shell: its toast, Models, and the endPrivate confirm, whose answers close it', async () => {
    const app = await started('chat:new')
    app.session.setDraft('/recall off')
    app.session.submit()
    expect(app.shell.toast?.text).toBe('/recall off — noted; this prototype doesn’t act on it')
    app.session.openLibrary()
    expect(app.shell.route).toBe('models')
    app.shell.go('sessions')
    app.session.onAction?.({ case: 'ask', kind: 'endPrivate' })
    expect(app.shell.confirm).toBe('endPrivate')
    app.keepRunning()
    expect(app.shell.confirm).toBeUndefined()
  })

  test('a header menu raises the shell’s scrim, and the scrim closes it', async () => {
    const app = await started('chat:new')
    app.session.toggleMenu('model')
    expect(app.shell.menuOpen).toBe(true)
    app.shell.closeMenus()
    expect(app.session.menu).toBeUndefined()
  })
})

test('a session replaced stops listening to the clock: chat:new’s, replaced by a private one three times', async () => {
  const app = await started('chat:new')
  const listening = () => (app.clock as unknown as { listeners: unknown[] }).listeners.length
  const before = listening()
  for (let index = 0; index < 3; index += 1) {
    app.session.leave({ type: 'private' })
    await settled()
  }
  expect(listening()).toBe(before)
})

describe("the session's scenarios and states in the composition (design 1v)", () => {
  const rowsOf = (app: AppViewModel) => app.session.face().rows

  test('chat: the Streaming session mid-reasoning -- the folder attached, the first exchange answered, the second recalling a past chat, its activity live, no answer yet', async () => {
    const app = await started('chat')
    const kinds = rowsOf(app).map((row) => row.kind)
    // Turn 2 in the CLI's order: the rewrite opens its activity, then its sources and its recall (drawn above the reply by the view).
    expect(kinds).toEqual(['notice', 'user', 'notice', 'notice', 'sources', 'activity', 'answer', 'user', 'activity', 'sources', 'recalled'])
    expect(app.session.attachments).toEqual(['.'])
    expect(rowsOf(app).filter((row) => row.kind === 'activity').at(-1)).toMatchObject({ live: true, summary: 'thinking · 3 steps' })
    expect(app.session.face().composer).toMatchObject({ status: 'thinking', streaming: true })
    expect(app.child.unanswered).toEqual([])
  })

  test('chat:retry: the permission card stands, the turn waiting for you', async () => {
    const app = await started('chat:retry')
    expect(rowsOf(app).find((row) => row.kind === 'permission')).toMatchObject({ title: 'Change a file?', answered: '', closed: false })
    expect(app.session.face().composer.status).toBe('waiting for you')
  })

  test('chat:wal: moved to the billed claude, two files attached, the cited answer with citation 1 open', async () => {
    const app = await started('chat:wal')
    expect(rowsOf(app)[0]).toMatchObject({ kind: 'notice', text: 'switched to claude -- from the next turn', billed: true })
    expect(rowsOf(app).find((row) => row.kind === 'answer')).toMatchObject({ cited: 1, footer: { model: 'claude', kind: 'api' } })
    expect(app.session.attachments).toEqual(['storage-engine-design.pdf', 'postgresql.conf'])
  })

  test("chat:crash: vast didn't load -- the old card, the CLI's words", async () => {
    const app = await started('chat:crash')
    expect(rowsOf(app).at(-1)).toMatchObject({ kind: 'error', title: 'vast didn’t load', loadFailed: true, instead: 'claude', canRetry: true })
  })

  test('chat:pr: Claude is asking, its options, Per batch picked as the old frame opens it, the turn waiting for you', async () => {
    const app = await started('chat:pr')
    const card = rowsOf(app).find((row) => row.kind === 'question')
    expect(card).toMatchObject({ label: 'Claude is asking', answered: [] })
    expect(card?.kind === 'question' ? card.items.map((item) => item.picked) : []).toEqual(['Per batch'])
    // A pick, not an answer: nothing is written until Answer.
    expect(app.child.calls.filter((call) => call.kind === 'chats')).toHaveLength(1)
    expect(app.session.model).toMatchObject({ name: 'claude', kind: 'subscription' })
  })

  test('chat:restarting: the kv chat read back, its child being stopped for the suite pair, an attach held, Send off, the inspector closed', async () => {
    const app = await started('chat:restarting')
    expect(rowsOf(app).map((row) => row.kind)).toEqual(['warning', 'user', 'answer', 'divider', 'notice', 'notice'])
    // Moved before any child had said its vocabulary, so the move is said in the Mac's words (its moveWords), as the old frame says it.
    expect(rowsOf(app).filter((row) => row.kind === 'notice').map((row) => (row.kind === 'notice' ? row.text : ''))).toEqual([
      "switching to suite pair (--suite pair) -- machine mode carries no suite, so the session restarts under it; the conversation carries on on the suite's chat member, and a local model loads again",
      'resume: a message could not be read and was skipped',
    ])
    expect(app.session.transcript.status).toEqual({ case: 'restarting' })
    expect(app.session.face().held).toMatchObject({ shown: true, words: 'Sent once the session is back: attach storage-engine-design.pdf' })
    expect(app.session.face().composer).toMatchObject({ placeholder: 'The session is restarting', status: 'restarting', pulse: true, inputOn: false })
    expect(app.session.inspectorOpen).toBe(false)
    expect(app.shell.sheet).toBeUndefined()
    expect(app.child.unanswered).toEqual([])
  })

  test('chat:notsent: the turn its closed child never took, marked not sent after the resume’s notice; the session back, Send again enabled', async () => {
    const app = await started('chat:notsent')
    expect(rowsOf(app).map((row) => row.kind)).toEqual(['warning', 'user', 'answer', 'divider', 'notice', 'user', 'notSent'])
    expect(rowsOf(app).at(-1)).toMatchObject({ kind: 'notSent', canResend: true, enabled: true })
    expect(app.session.transcript.status).toEqual({ case: 'ready' })
    expect(app.child.calls.filter((call) => call.kind === 'chats')).toHaveLength(2)
  })

  test('chat:notsent: Send again is answered by the returned child, as the chat-kv recipe recorded that turn', async () => {
    const app = await started('chat:notsent')
    app.session.resend(scenarioTurns.kv)
    app.clock.advance(60)
    await settled()
    expect(app.session.transcript.status).toEqual({ case: 'ready' })
    const last = rowsOf(app).at(-1)
    expect(last?.kind).toBe('answer')
    expect(last?.kind === 'answer' ? last.text : '').toMatch(/^A 16-bit cache is twice the 8-bit one/)
    expect(app.child.unanswered).toEqual([])
  })

  test('chat:ended: the answer cut mid-stream, Session ended in the CLI’s words with Start again, two lines held for it', async () => {
    const app = await started('chat:ended')
    expect(rowsOf(app).at(-1)).toMatchObject({ kind: 'ended', text: 'apogee exited with status 2: fatal: the model went away', canStartAgain: true })
    expect(app.session.face().held.words).toBe('Sent when the session starts again: temperature 0.4 · compact')
    expect(app.session.face().composer).toMatchObject({ placeholder: 'The session has ended', status: 'session ended', inputOn: false })
  })

  test('chat:ended: Start again -- the held setting and compact reach the next child, which answers each in the CLI’s words', async () => {
    const app = await started('chat:ended')
    await app.session.start()
    for (let tick = 0; tick < 6; tick += 1) {
      app.clock.advance(1)
      await settled()
    }
    const notices = rowsOf(app).flatMap((row) => (row.kind === 'notice' ? [row.text] : []))
    expect(notices).toContain('temperature: 0.4 -- from the next turn')
    expect(notices).toContain('history compacted')
    expect(app.session.face().held.shown).toBe(false)
    expect(app.child.unanswered).toEqual([])
  })

  test('chat:compacting: two saved chats compacting side by side, retry’s first; its open session Streaming at its earlier frame', async () => {
    const app = await started('chat:compacting')
    const chat = await started('chat')
    const reasoning = (target: AppViewModel) => {
      const activity = target.session.face().rows.filter((row) => row.kind === 'activity').at(-1)
      return activity?.kind === 'activity' ? activity.reasoning : ''
    }
    expect(Object.keys(app.compactions.compactions)).toHaveLength(2)
    expect(app.child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments.slice(0, 2).join(' '))).toEqual(['chats compact', 'chats compact'])
    expect(reasoning(app).length).toBeGreaterThan(0)
    expect(reasoning(chat).startsWith(reasoning(app))).toBe(true)
    expect(reasoning(chat).length).toBeGreaterThan(reasoning(app).length)
  })
})
