import configScan from '@recorded/business/reads/Fixtures/config-scan.txt?raw'
import { describe, expect, test } from 'vitest'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel, scenarioTurns } from '../../../source/presentation/app/ViewModels/AppViewModel'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'
import { kvChat, sessionSamples } from '../../../source/presentation/session/Previews/SessionPresets'

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
    // 1g: the library reads its list and the model facts before any session opens, as the Mac's does, and the inspector's once the session has said its suite.
    expect(app.child.calls.map((call) => `${call.kind}: ${call.arguments.join(' ')}`)).toEqual([
      'reads: chats list',
      'reads: models list',
      'chats: chat --output-format stream-json --input-format stream-json --tools',
      'commands: config get models.default_suite',
      'reads: models status --suite fast',
      'reads: models info claude',
      'reads: chats recall',
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

describe('the Sessions place in the composition (design 1g)', () => {
  test('every screen’s sidebar lists the recorded chats by date: the monitor’s, a chat preset’s, and the console’s', async () => {
    for (const start of ['monitor', 'chat:new', 'chat:pr', 'console']) {
      const app = await started(start)
      expect(app.library.listFace().count, start).toBe(13)
      expect(app.library.listFace().groups.map((group) => group.title), start).toEqual(['Today', 'Yesterday', 'Previous 7 days', 'Earlier'])
    }
  })

  test('the open session is its listed row, and its header takes the list’s name: chat is "TaskGroup timeouts in the ingest runner"', async () => {
    const app = await started('chat')
    expect(app.session.face().header.title).toBe('TaskGroup timeouts in the ingest runner')
    expect(app.library.listFace().groups[0]?.rows.find((row) => row.current)?.title).toBe('TaskGroup timeouts in the ingest runner')
  })

  test('⌥⌘I, handed by the shell, shows and hides the inspector on a chat preset', async () => {
    const app = await started('chat:restarting')
    expect(app.session.inspectorOpen).toBe(false)
    expect(app.shell.press({ key: 'i', modifiers: ['option', 'command'] })).toBe(true)
    expect(app.session.inspectorOpen).toBe(true)
    app.shell.press({ key: 'i', modifiers: ['option', 'command'] })
    expect(app.session.inspectorOpen).toBe(false)
  })

  test('chat:compacting: retry’s and kv’s rows busy, kv’s menu open on Stop compacting, as the old frame opens it', async () => {
    const app = await started('chat:compacting')
    const rows = app.library.listFace().groups.flatMap((group) => group.rows)
    expect(rows.filter((row) => row.busy).map((row) => row.title)).toEqual(['Make fetch retries configurable', 'Where llama.cpp sizes the KV cache'])
    const kv = rows.find((row) => row.menuOpen)
    expect(kv?.title).toBe('Where llama.cpp sizes the KV cache')
    expect(kv?.items.map((item) => item.label)).toContain('Stop compacting')
    expect(app.shell.menu).toBe(`row:${kv?.id}`)
  })

  test('execute: "New execute session" over the play session, suite coding, ~/code/ingest, orchestrating, and its line', async () => {
    const app = await started('execute')
    expect(app.shell.sheet).toBe('execute')
    expect(app.session.face().header.banner).toBe('execute · suite coding · orchestrating · in ingest')
    const door = app.library.door()
    expect(door).toMatchObject({ suite: 'coding', folder: '~/code/ingest', orchestrate: true, canOrchestrate: true, command: 'cd ~/code/ingest && apogee execute --tools --suite coding --orchestrate' })
    expect(door.suites[0]?.label).toBe('The default suite (coding)')
    expect(door.models[0]?.label).toBe('The suite’s chat member')
    expect(app.child.unanswered).toEqual([])
  })

  test('execute: Open opens the door’s session, the catalog read first, the door closed', async () => {
    const app = await started('execute')
    await app.library.openExecute()
    await settled()
    expect(app.shell.sheet).toBeUndefined()
    expect(app.child.calls.filter((call) => call.kind === 'chats').at(-1)?.arguments).toEqual(['execute', '--output-format', 'stream-json', '--input-format', 'stream-json', '--tools', '--suite', 'coding', '--orchestrate'])
    expect(app.child.unanswered).toEqual([])
  })

  test('console: the console over the Streaming session at its starting frame -- models list’s output, apogee reset refused, `models ` typed and ten words offered', async () => {
    const app = await started('console')
    expect(app.shell.overlay).toBe('console')
    const face = app.console.face()
    expect(face.lines[0]).toMatchObject({ kind: 'command', text: 'apogee models list' })
    expect(face.lines.slice(-3).map((line) => [line.kind, line.text])).toEqual([
      ['note', 'Done.'],
      ['command', 'apogee reset'],
      ['problem', 'Not run here: deletes the data directory the running app stands on -- run it at a prompt, with the app closed.'],
    ])
    expect(face.input).toBe('models ')
    expect(face.suggestions).toEqual(['list', 'info', 'status', 'rename', 'pull', 'delete', 'quantize', 'convert', 'repair', 'migrate'])
    expect(app.session.transcript.status.case).toBe('thinking')
    expect(app.child.unanswered).toEqual([])
    expect(app.consoleChild.calls.map((call) => `${call.kind}: ${call.arguments.join(' ')}`), 'the console’s lines reach its own child').toEqual(['jobs: models list', 'commands: __complete models '])
    expect(app.consoleChild.unanswered).toEqual([])
  })

  test('off Sessions no row is current, as the old list draws it (`s.screen === \'chat\'`); back on Sessions the open session’s row is again', async () => {
    const app = await started('chat')
    const current = () => app.library.listFace().groups.flatMap((group) => group.rows.filter((row) => row.current).map((row) => row.title))
    expect(current()).toEqual(['TaskGroup timeouts in the ingest runner'])
    app.shell.go('models')
    expect(current()).toEqual([])
    app.shell.go('sessions')
    expect(current()).toEqual(['TaskGroup timeouts in the ingest runner'])
  })

  for (const overlay of ['console', 'monitor'] as const) {
    test(`a row's act leaves the ${overlay} open, as the old act does ({ screen: 'chat', menu: null }): Details… shows Sessions, its sheet over it`, async () => {
      const app = await started(overlay)
      app.shell.go('models')
      expect(app.shell.overlay).toBe(overlay)
      app.library.rowAct(kvChat, 'details')
      await settled()
      expect(app.shell.overlay).toBe(overlay)
      expect(app.shell.route).toBe('sessions')
      expect(app.shell.sheet).toBe('chatInfo')
    })
  }

  test('Stop compacting stays on the place shown, as the old stopCompacting only says', async () => {
    const app = await started('chat:compacting')
    app.shell.go('models')
    app.library.rowAct(kvChat, 'stopCompacting')
    await settled()
    await settled()
    expect(app.shell.route).toBe('models')
    expect(app.shell.toast?.text).toBe('Not compacted: stopped')
  })

  test('the console answers on every screen, as the default says: at chat, a line nobody recorded prints the prototype’s "Done."', async () => {
    const app = await started('chat')
    app.shell.open('console')
    app.console.setInput('doctor')
    const ran = app.console.run()
    app.clock.advance(2)
    await ran
    expect(app.console.face().lines.map((line) => [line.kind, line.text])).toEqual([
      ['command', 'apogee doctor'],
      ['note', 'Done.'],
    ])
  })

  test('at console, a line with a recorded answer prints it: Clear, then config scan says config-scan.txt, then "Done."', async () => {
    const app = await started('console')
    app.console.clear()
    app.console.setInput('config scan')
    const ran = app.console.run()
    app.clock.advance(20)
    await ran
    const lines = app.console.face().lines
    expect(lines[0]).toMatchObject({ kind: 'command', text: 'apogee config scan' })
    expect(lines[1]).toMatchObject({ kind: 'out', text: configScan.split('\n')[0] })
    expect(lines.at(-1)).toMatchObject({ kind: 'note', text: 'Done.' })
  })

  test("the console's catch-all is the console's alone: at chat, a row's compaction nobody recorded is not said done", async () => {
    const app = await started('chat')
    app.library.rowAct(kvChat, 'compact')
    void app.library.confirm()
    for (let tick = 0; tick < 3; tick += 1) {
      app.clock.advance(1)
      await settled()
    }
    expect(app.shell.toast?.text).toBe(`Not compacted: no recorded answer for apogee chats compact ${kvChat} --yes`)
  })

  test('a list row opens its chat through the session’s leave: a private conversation asks first', async () => {
    const app = await started('chat:private')
    app.session.setDraft('a secret')
    app.session.submit()
    app.library.open(kvChat)
    expect(app.shell.confirm).toBe('endPrivate')
    app.keepRunning()
    expect(app.session.privateMode).toBe('nothingWritten')
  })
})

test("chat:play: the inspector's turn rows are the play's stages, by their count, from the replayed events (design 1g)", async () => {
  const app = await started('chat:play')
  const rows = app.library.inspector().turn.rows
  expect(rows.map((row) => row.label)).toEqual(['Stage 1/2', 'Stage 2/2', 'Usage'])
  expect(rows[0]?.value).toMatch(/^summarize — asking utility \(helper\): /)
  expect(app.library.inspector().turn.status).toEqual({ words: 'done', tone: 'tertiary' })
})

test("chat:retry: the inspector's turn waits for you at the gate -- the reads, then write_file needing your OK, in the old row's words (design 1g)", async () => {
  const app = await started('chat:retry')
  const face = app.library.inspector().turn
  expect(face.status).toEqual({ words: 'waiting for you', tone: 'warning' })
  expect(face.rows.map((row) => [row.label, row.value, row.sub])).toEqual([
    ['Thinking', 'default', ''],
    ['read_file', 'config.toml', 'done'],
    ['read_file', 'src/ingest/runner.py', 'done'],
    ['write_file', 'src/ingest/runner.py', '+11 −3 · needs your OK'],
    ['Usage', 'reported when the turn ends', ''],
  ])
})

test("chat:crash: the inspector's turn failed -- vast, in the error color, the CLI's words under it, and no usage row, as the old failed frame (design 1g)", async () => {
  const app = await started('chat:crash')
  const face = app.library.inspector().turn
  expect(face.status).toEqual({ words: 'failed', tone: 'error' })
  expect(face.rows.map((row) => [row.label, row.value, row.tone])).toEqual([['Model', 'vast', 'error']])
  expect(face.rows[0]?.sub).toMatch(/^vast: could not load the model at /)
})

describe('the Models place in the composition (design 1h)', () => {
  test('models: Backends over the store world, llama3.2-1b-Q4KM’s details open, the Streaming session behind it, nothing unanswered', async () => {
    const app = await started('models')
    expect(app.shell.route).toBe('models')
    expect(app.models.tab).toBe('backends')
    expect(app.models.face().backends.detail.name).toBe('llama3.2-1b-Q4KM')
    expect(app.session.connection.case).toBe('connected')
    expect(app.child.unanswered).toEqual([])
  })

  test('each Models preset opens on its tab, sheet or confirm, as the old applyStart sets it', async () => {
    const expectations: ReadonlyArray<readonly [string, string, string, string]> = [
      ['models:downloads', 'jobs', 'none', 'none'],
      ['models:store', 'store', 'none', 'none'],
      ['models:convert', 'store', 'storeConvert', 'none'],
      ['models:register', 'store', 'storeRegister', 'none'],
      ['models:roles', 'roles', 'none', 'none'],
      ['models:delete', 'store', 'none', 'storeDelete'],
      ['models:notran', 'jobs', 'none', 'none'],
      ['download', 'backends', 'download', 'none'],
    ]
    for (const [start, tab, sheet, confirm] of expectations) {
      const app = await started(start)
      expect([start, app.shell.route, app.models.tab, app.shell.sheet ?? 'none', app.shell.confirm ?? 'none']).toEqual([start, 'models', tab, sheet, confirm])
      expect(app.child.unanswered, start).toEqual([])
    }
  })

  test('models:downloads: the Llama conversion stands mid-run, its F16 named and its quantization running; the sidebar counts it on Models', async () => {
    const app = await started('models:downloads')
    const card = app.models.face().jobs.cards[0]
    expect(card?.repo).toBe('Llama3.2-1B-Instruct/safetensors/edaba290ce7f')
    expect(card?.steps.map((step) => step.state)).toEqual(['done', 'running', 'waiting'])
    expect(app.placeRuns).toEqual({ models: 1 })
    const chat = await started('chat')
    expect(chat.placeRuns).toEqual({ models: 0 })
  })

  test('models:downloads and models:notran with no apogee: nothing starts, so no card stands running and the sidebar counts nothing', async () => {
    for (const start of ['models:downloads', 'models:notran']) {
      const app = await started(start, 'missing')
      expect([start, app.models.face().jobs]).toEqual([start, { cards: [], running: 0 }])
      expect(app.placeRuns).toEqual({ models: 0 })
    }
  })

  test('models:convert: the convert sheet on the Llama snapshot -- GGUF f16, then Q4_K_M, registered; Convert runs it, and it lands in Stored weights', async () => {
    const app = new AppViewModel(presetFor({ start: 'models:convert', still: false, apogee: 'installed' }).preset, modeRules, undefined, sessionSamples)
    await app.start()
    await settled()
    expect(app.models.store.sheet()?.command).toBe('apogee models convert Llama3.2-1B-Instruct/safetensors/edaba290ce7f --register-with Q4_K_M')
    app.stop()
    await app.models.store.goAsync()
    expect(app.shell.sheet).toBeUndefined()
    expect(app.models.tab).toBe('jobs')
    await settled()
    app.clock.advance(40)
    await settled()
    expect(app.models.face().jobs.cards[0]?.status).toMatch(/^done/)
    const llama = app.models.store.face().groups.find((group) => group.dir === 'Llama3.2-1B-Instruct')
    expect(llama?.sets.map((set) => set.state)).toEqual(['backend Llama3.2-1B-Instruct-F16', 'backend Llama3.2-1B-Instruct-Q4KM', 'used by a conversion'])
    expect(app.child.unanswered).toEqual([])
  })

  test('download: the sheet on the recorded pull; Start download runs it on Jobs, the sidebar counts it, and its three sets land', async () => {
    const app = new AppViewModel(presetFor({ start: 'download', still: false, apogee: 'installed' }).preset, modeRules, undefined, sessionSamples)
    await app.start()
    await settled()
    app.stop()
    expect(app.models.face().downloadSheet.repo).toBe('meta-llama/Llama-3.2-1B-Instruct')
    app.models.startDownload()
    expect(app.shell.sheet).toBeUndefined()
    expect(app.placeRuns).toEqual({ models: 1 })
    app.clock.advance(70)
    await settled()
    expect(app.placeRuns).toEqual({ models: 0 })
    expect(app.shell.toast?.text, 'the old done toast, from the documents the run reported').toBe('Llama3.2-1B-Instruct · Q4_K_M is ready in the library')
    // The snapshot a conversion of the same chain used folds away, as models list folds it.
    expect(app.models.store.face().groups.find((group) => group.dir === 'Llama3.2-1B-Instruct')?.sets.map((set) => [set.short, set.state])).toEqual([
      ['gguf/3f0c2a9b7e41', 'backend Llama3.2-1B-Instruct-F16'],
      ['gguf/8d51e6c04a27', 'backend Llama3.2-1B-Instruct-Q4KM'],
    ])
    expect(app.models.store.face().fold.text).toBe('1 SafeTensors set a conversion has used is folded away, as models list folds it.')
    expect(app.child.unanswered).toEqual([])
  })

  test('the inspector’s Open in library lands on Backends at the session’s model, and Change helpers on Roles (1g’s hand-off)', async () => {
    const app = await started('chat')
    app.library.openModels?.('backends', 'root')
    await settled()
    expect(app.shell.route).toBe('models')
    expect(app.models.tab).toBe('backends')
    expect(app.models.selected).toBe('root')
    app.shell.go('sessions')
    app.library.openModels?.('roles')
    expect(app.shell.route).toBe('models')
    expect(app.models.tab).toBe('roles')
  })

  test('Use in the open session: the session’s model set, Sessions shown, as the old pickModelFor does', async () => {
    const app = await started('models')
    await app.models.act('useInSession')
    expect(app.shell.route).toBe('sessions')
    expect(JSON.stringify(app.session.face()), 'the set written, or held while the turn runs').toContain('llama3.2-1b-Q4KM')
  })

  test('a Models confirm answered in the composition closes the host: models:delete’s second door runs --and-backends', async () => {
    const app = await started('models:delete')
    await app.confirmModels('storeDelete', true)
    expect(app.shell.confirm).toBeUndefined()
    expect(app.child.calls.find((call) => call.arguments.includes('--and-backends'))?.arguments).toEqual(['models', 'delete', 'Llama3.2-1B-Instruct/gguf/8d51e6c04a27', '--yes', '--and-backends'])
    expect(app.shell.toast?.text).toBe('Deleted Llama3.2-1B-Instruct/gguf/8d51e6c04a27')
  })
})
