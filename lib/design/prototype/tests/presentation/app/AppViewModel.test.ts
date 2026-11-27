import configScan from '@recorded/business/reads/Fixtures/config-scan.txt?raw'
import { describe, expect, test } from 'vitest'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel, scenarioTurns } from '../../../source/presentation/app/ViewModels/AppViewModel'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'
import { kvChat, sessionSamples } from '../../../source/presentation/session/Previews/SessionPresets'
import { chord } from '../../../source/presentation/shell/ViewModels/KeyMap'

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
    expect(app.placeRuns).toEqual({ models: 1, tasks: 0, training: 0, video: 0, voice: 0 })
    const chat = await started('chat')
    expect(chat.placeRuns).toEqual({ models: 0, tasks: 0, training: 0, video: 0, voice: 0 })
  })

  test('models:downloads and models:notran with no apogee: nothing starts, so no card stands running and the sidebar counts nothing', async () => {
    for (const start of ['models:downloads', 'models:notran']) {
      const app = await started(start, 'missing')
      expect([start, app.models.face().jobs]).toEqual([start, { cards: [], running: 0 }])
      expect(app.placeRuns).toEqual({ models: 0, tasks: 0, training: 0, video: 0, voice: 0 })
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
    expect(app.placeRuns).toEqual({ models: 1, tasks: 0, training: 0, video: 0, voice: 0 })
    app.clock.advance(70)
    await settled()
    expect(app.placeRuns).toEqual({ models: 0, tasks: 0, training: 0, video: 0, voice: 0 })
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

describe('Settings in the composition (design 1i)', () => {
  const panes: Readonly<Record<string, string>> = {
    'settings:general': 'general',
    'settings:appearance': 'appearance',
    'settings:media': 'media',
    'settings:backends': 'backends',
    'settings:fixing': 'backends',
    'settings:fixfailed': 'backends',
    'settings:permissions': 'permissions',
    'settings:nosearch': 'web',
    'settings:xcode': 'xcode',
    'settings:roles': 'roles',
    'settings:web': 'web',
    'settings:memory': 'memory',
    'settings:keyboard': 'keyboard',
  }

  for (const [preset, pane] of Object.entries(panes)) {
    test(`${preset} opens the Settings window on ${pane}, over the Streaming session at its starting frame, every read answered`, async () => {
      const app = await started(preset)
      expect(app.shell.settingsOpen).toBe(true)
      expect(app.settings.pane).toBe(pane)
      expect(app.shell.route).toBe('sessions')
      expect(app.session.transcript.status.case, 'the Streaming session behind it, mid-reasoning (the old progress 15)').toBe('thinking')
      expect(app.settings.face().foot).not.toEqual([])
      expect(app.child.unanswered).toEqual([])
    })
  }

  test('every preset pins Automatic to dark, as the old prototype draws it', async () => {
    const app = await started('settings:appearance')
    expect(app.theme.pinned).toBe(true)
    app.settings.theme.pick('auto')
    app.theme.systemChanged('light')
    expect(app.theme.theme).toBe('dark')
  })

  test('⌘, opens the window on any screen and reads it; Done closes it, the key being typed with it', async () => {
    const app = await started('chat')
    expect(app.shell.settingsOpen).toBe(false)
    app.shell.press({ key: ',', modifiers: ['command'] })
    await settled()
    expect(app.shell.settingsOpen).toBe(true)
    expect(app.settings.face().backends.health.summary).toBe('0 failing, 2 to look at')
    app.settings.editKey('anthropic')
    app.closeSettings()
    expect(app.shell.settingsOpen).toBe(false)
    expect(app.settings.face().backends.keys[0]?.editing).toBe(false)
    expect(app.child.unanswered).toEqual([])
  })

  test('an Appearance change restyles the whole window at once: the window\'s theme service is the pane\'s', async () => {
    const app = await started('settings:appearance')
    app.settings.theme.pickColor('surface.window', '#202020')
    expect(app.theme.style['--surface-window']).toBe('#202020')
    app.settings.theme.setTextSize('xl')
    expect(app.theme.style['--t-body']).toBe('15px')
    app.settings.theme.resetAll()
    app.settings.theme.setTextSize('m')
    expect(app.theme.style['--surface-window']).toBe('#131415')
    expect(app.theme.style['--t-body']).toBe('13px')
  })

  test("settings:fixing: check --fix running, working, Stop on; settings:fixfailed: the run ended failed, its words the red line and the toast", async () => {
    const fixing = await started('settings:fixing')
    // One tick in, the run has printed its first line, which the busy line shows, as the Mac's does.
    expect(fixing.settings.face().backends.health).toMatchObject({ fixing: true, busy: 'nothing to fix', canStop: true })
    const failed = await started('settings:fixfailed')
    expect(failed.settings.face().failure).toBe('it exited with status 1')
    expect(failed.settings.face().backends.health).toMatchObject({ fixing: false, summary: '1 failing, 2 to look at' })
    expect(failed.shell.toast?.text).toBe('check --fix failed: it exited with status 1')
  })

  test('settings:nosearch: the web search address unset, said not set', async () => {
    const app = await started('settings:nosearch')
    expect(app.settings.face().web.search).toBe('not set')
  })

  test('settings:xcode: check\'s Xcode section, the preset unregistered, the registration\'s fields', async () => {
    const app = await started('settings:xcode')
    const xcode = app.settings.face().xcode
    expect(xcode.rows).toHaveLength(4)
    expect(xcode.tools.canRegister).toBe(true)
    expect(xcode.agent.fields[0]).toMatchObject({ label: 'Name', value: 'Apogee (dev)' })
  })

  test("Models & roles: the Models place reads its roles for the slot, and Open models library goes to Models' Backends, the window closed", async () => {
    const app = await started('settings:roles')
    expect(app.models.face().roles.rows.map((row) => row.key)).toEqual(['chat', 'embedding', 'extraction', 'vision', 'transcription', 'utility'])
    app.settings.showLibrary()
    expect(app.shell.settingsOpen).toBe(false)
    expect(app.shell.route).toBe('models')
    expect(app.models.tab).toBe('backends')
  })

  test("Keyboard: the shell's keys and Sessions' five, every one named by the design's list", async () => {
    const app = await started('settings:keyboard')
    expect(app.settings.face().keyboard).toHaveLength(23)
  })

  test("Models' Add an API key… opens Settings on Backends & keys with that provider's key field open, as the old one sets keyEdit; Manage key… opens the bare pane (1h's hand-off)", async () => {
    const app = await started('models')
    await app.models.openBackend('claude')
    await app.models.act('addKey')
    await settled()
    expect(app.shell.settingsOpen).toBe(true)
    expect(app.settings.pane).toBe('backends')
    expect(app.settings.face().backends.keys.filter((row) => row.editing).map((row) => row.provider)).toEqual(['anthropic'])
    app.closeSettings()
    // The worlds keep no keyed API backend, so Manage key… is pressed on the same row: the act's switch, not the offer.
    await app.models.act('manageKey')
    await settled()
    expect(app.shell.settingsOpen).toBe(true)
    expect(app.settings.pane).toBe('backends')
    expect(app.settings.face().backends.keys.filter((row) => row.editing)).toEqual([])
  })

  test('Forget all summaries… asks in the shell\'s confirm host; yes runs the forget and closes it', async () => {
    const app = await started('settings:memory')
    await app.settings.askToForget()
    expect(app.shell.confirm).toBe('settingsForget')
    await app.confirmSettings('settingsForget')
    expect(app.shell.confirm).toBeUndefined()
    expect(app.shell.toast?.text).toMatch(/^forgot 8 chat summaries/)
  })

  test("Register… opens the shell's sheet; Register closes it and runs the provider's register", async () => {
    const app = await started('settings:backends')
    app.settings.askToRegister('claude')
    expect(app.shell.sheet).toBe('registerProvider')
    await app.settings.register()
    expect(app.shell.sheet).toBeUndefined()
    expect(app.shell.toast?.text).toBe('registered claude (claude-cli)')
  })
})

describe('the Training place in the composition (design 1j)', () => {
  test('each of the ten presets opens Training on its tab and state, the Streaming session behind it, nothing unanswered', async () => {
    const opens: ReadonlyArray<readonly [string, string, string]> = [
      ['training', 'runs', 'bar: [train] iter 2/4 · loss 0.6200 · lr 1.00e-04 · 10.0 it/s'],
      ['training:datasets', 'datasets', 'dataset: heat-notes'],
      ['training:pipelines', 'pipelines', 'live: pipe-20261026-074112'],
      ['training:cycle', 'cycle', 'passes: 2'],
      ['training:stalelock', 'cycle', 'lock: /Users/someone/.apogee/training/cycle/cycle.lock'],
      ['training:unreadable', 'cycle', 'error: The cycle history could not be read: /Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array'],
      ['training:cyclepass', 'pipelines', 'page: cycle-20261109-041521'],
      ['training:passrunning', 'pipelines', 'page: cycle-20261109-041521'],
      ['training:resumeheld', 'pipelines', 'hold: This pipeline is running now (process 67959); a resume is refused while it runs.'],
      ['training:regimes', 'regimes', 'regime: foundation'],
    ]
    for (const [start, tab, state] of opens) {
      const app = await started(start)
      const face = app.training.face()
      const shown: Record<string, string> = {
        bar: face.bar.line,
        dataset: face.datasets.detail.name,
        live: face.pipelines.live.id,
        passes: String(face.cycle.passes.length),
        lock: face.cycle.lock,
        error: face.cycle.error,
        page: face.pipelines.page.id,
        hold: face.pipelines.page.resume.hold,
        regime: face.regimes.detail.name,
      }
      const [key] = state.split(': ')
      expect([start, app.shell.route, face.tab, `${key}: ${shown[key ?? ''] ?? ''}`]).toEqual([start, 'training', tab, state])
      expect(app.session.face().empty.launched, `${start}: the Streaming session behind`).toBe(true)
      expect(app.shell.confirm, start).toBeUndefined()
      expect(app.child.unanswered, start).toEqual([])
    }
  })

  test('training:regimes is training’s frame on Regimes: the run New run… started, training, its bar and Stop, as the old reaches it by clicking Regimes on training (design 1s)', async () => {
    const regimes = (await started('training:regimes')).training.face()
    const runs = (await started('training')).training.face()
    expect([regimes.tab, regimes.regimes.detail.name]).toEqual(['regimes', 'foundation'])
    expect(regimes.bar).toEqual(runs.bar)
    expect(regimes.bar.line).toBe('[train] iter 2/4 · loss 0.6200 · lr 1.00e-04 · 10.0 it/s')
  })

  test('training:pipelines and training:passrunning: their pages, the stage at its iteration and the pass that runs elsewhere', async () => {
    const pipelines = await started('training:pipelines')
    expect(pipelines.training.face().pipelines.page.stages.map((stage) => stage.line)).toEqual(['passed · cumulative 100%', 'training · iter 2/4 · loss 0.6200 · 10.0 it/s'])
    const running = await started('training:passrunning')
    expect(running.training.face().pipelines.page.endLine).toBe('A cycle pass, running now (process 67959): its candidate is the cycle’s, judged by its dual gate when the pass ends -- never from Runs.')
  })

  test("the sidebar's Training badge: the registry's train runs, pipelines and passes, never a pull or a promote", async () => {
    const training = await started('training')
    expect(training.placeRuns).toEqual({ models: 0, tasks: 0, training: 1, video: 0, voice: 0 })
    const pipelines = await started('training:pipelines')
    expect(pipelines.placeRuns).toEqual({ models: 0, tasks: 0, training: 1, video: 0, voice: 0 })
    const cycle = await started('training:cycle')
    expect(cycle.placeRuns).toEqual({ models: 0, tasks: 0, training: 0, video: 0, voice: 0 })
    cycle.training.requestPass()
    expect(cycle.shell.confirm).toBe('train')
    await cycle.confirmTraining('train')
    expect(cycle.shell.confirm).toBeUndefined()
    expect(cycle.placeRuns).toEqual({ models: 0, tasks: 0, training: 1, video: 0, voice: 0 })
    const datasets = await started('training:datasets')
    datasets.training.newPull()
    expect(datasets.shell.sheet).toBe('pull')
    datasets.training.setPullRef('someone/heat-notes')
    datasets.training.pull()
    expect(datasets.shell.sheet).toBeUndefined()
    expect(datasets.placeRuns).toEqual({ models: 0, tasks: 0, training: 0, video: 0, voice: 0 })
    expect(datasets.registry.children('job').map((job) => job.label)).toEqual(['apogee datasets pull someone/heat-notes'])
  })

  test('⌘9 opens Training through the key map, and the place reads as it comes into view', async () => {
    const app = await started('chat')
    const before = app.child.calls.filter((call) => call.arguments.join(' ') === 'train status').length
    expect(app.shell.press(chord('9', 'command'))).toBe(true)
    await settled()
    expect(app.shell.route).toBe('training')
    expect(app.child.calls.filter((call) => call.arguments.join(' ') === 'train status').length).toBe(before + 1)
  })

  test("a refused promote raises the shell's confirm, Change the promotion… reopens the sheet, OK lets it go", async () => {
    const app = await started('training')
    await app.training.stop()
    await settled()
    app.training.newPromotion()
    expect(app.shell.sheet).toBe('promote')
    app.training.setPromote({ backend: 'based' })
    app.training.promote()
    expect(app.shell.sheet).toBeUndefined()
    app.clock.advance(2)
    await settled()
    await settled()
    expect(app.shell.confirm).toBe('promoteRefused')
    await app.confirmTraining('promoteRefused')
    expect([app.shell.confirm, app.shell.sheet]).toEqual([undefined, 'promote'])
    expect(app.training.face().promoteSheet.error).toMatch(/^apogee train: the eval gate failed for run 20261021-051826/)
    app.training.cancelPromote()
    expect(app.shell.sheet).toBeUndefined()
  })
})

describe('the Video place in the composition (design 1k)', () => {
  test('video: Clips, the final made from its draft shown, paused 2.1 s in, the paper boat’s draft on the right, nothing said', async () => {
    const app = await started('video')
    expect(app.shell.route).toBe('video')
    const face = app.video.face()
    expect(face.clip).toMatchObject({ shown: true, file: 'lighthouse-dusk.mp4', preset: 'FINAL', modeText: 'from text' })
    expect(face.clip.player).toMatchObject({ playing: false, time: '0:02.1 / 0:05.0', pct: 42 })
    expect(face.form).toMatchObject({ heading: 'New clip', out: 'paper-boat-drifting-draft.mp4', preset: 'draft' })
    expect(face.list).toHaveLength(5)
    expect(app.shell.toast).toBeUndefined()
    expect(app.child.unanswered).toEqual([])
  })

  test('video:run: the draft made final, its run a job on the shared list at step 38 of 50 -- the sidebar counts it, the monitor lists it, and its Stop there stops it', async () => {
    const app = await started('video:run')
    const face = app.video.face()
    expect(face.showRun).toBe(true)
    expect(face.run).toMatchObject({ shown: true, file: 'lighthouse-dusk-3.mp4', status: 'step 38 of 50', live: true })
    expect(face.form.heading).toBe('From lighthouse-dusk-draft.mp4')
    expect(app.placeRuns).toEqual({ models: 0, tasks: 0, training: 0, video: 1, voice: 0 })
    const run = app.monitor.face().models.rows.find((row) => row.run !== undefined)
    expect(run).toMatchObject({ name: 'Wan2.2-TI2V-5B · generating', detail: 'a video run this app supervises · mps · bfloat16 · the CLI reports no size for it', action: 'Stop the run' })
    if (run) app.monitor.act(run)
    await settled()
    expect(app.video.face().run.status).toBe('stopped by you')
    expect(app.placeRuns.video).toBe(0)
  })

  test('consent: rain run again, refused for peft by the CLI, the shared sheet asking in its words; Not now closes it and starts nothing', async () => {
    const app = await started('consent')
    expect(app.shell.sheet).toBe('runtimeConsent')
    expect(app.video.face().consent).toMatchObject({ shown: true, title: 'Install what video needs?', said: 'a LoRA loads through peft, which the video runtime does not have yet -- apogee train setup --with video' })
    expect(app.video.face().clip.file).toBe('rain-window.mp4')
    expect(app.video.face().run.shown).toBe(false)
    app.video.decline()
    expect(app.shell.sheet).toBeUndefined()
    expect(app.shell.toast?.text).toBe('Not installed. apogee train setup --with video installs it whenever you want it')
    expect(app.registry.children()).toEqual([])
  })

  test('video:install: the refusal played, Install pressed: its card running four ticks in, the sheet closed, the badge not counting an install', async () => {
    const app = await started('video:install')
    expect(app.shell.sheet).toBeUndefined()
    expect(app.video.face().install).toMatchObject({ shown: true, state: 'running', headline: 'Installing the video set' })
    expect(app.registry.children('job').map((job) => job.label)).toEqual(['apogee train setup'])
    expect(app.placeRuns.video).toBe(0)
  })

  test("Settings' Install… on the video runtime's row opens the same sheet over the Video place, in check's words; Install from there runs the install and shows Video", async () => {
    const app = await started('chat')
    app.openSettings('backends')
    await settled()
    app.settings.installRuntime('runtime')
    expect(app.shell.sheet).toBe('runtimeConsent')
    expect(app.video.face().consent.said).toBe('Video · runtime: not set up -- torch is not installed in the Python environment at /Users/someone/.apogee/training/venv')
    app.video.install()
    expect(app.shell.sheet).toBeUndefined()
    expect(app.shell.route).toBe('video')
    expect(app.registry.children('job').map((job) => job.label)).toEqual(['apogee train setup'])
  })

  test('the monitor lists the place’s clip only once its pipeline has loaded, as the old lists `vrun` past `load`: not while it loads, nor while a refusal plays; another’s generate by its label', async () => {
    const loading = await started('video')
    loading.video.act('final', '/Users/someone/Movies/Apogee/lighthouse-dusk-draft.mp4')
    loading.video.generate()
    expect(loading.registry.children('job').map((job) => job.label)).toEqual(['apogee video generate'])
    expect(loading.monitor.face().models.rows.filter((row) => row.run !== undefined)).toEqual([])
    // A generate another supervisor runs (the console's, typed) is listed by its label, as the badge counts it.
    loading.registry.register('job', 'apogee video generate', async () => {}, ['video', 'generate', '-o', '/Users/someone/Movies/Apogee/x.mp4', '--', 'x'])
    expect(loading.monitor.face().models.rows.filter((row) => row.run !== undefined).map((row) => row.name)).toEqual(['apogee video generate'])
    const refused = await started('video')
    refused.video.act('again', '/Users/someone/Movies/Apogee/rain-window.mp4')
    refused.video.generate()
    expect(refused.registry.children('job').map((job) => job.label)).toEqual(['apogee video generate'])
    expect(refused.monitor.face().models.rows.filter((row) => row.run !== undefined)).toEqual([])
  })

  test("Settings' Install… while a clip is made: Install starts the install beside the run, as the old one checks only for a bootstrap", async () => {
    const app = await started('video:run')
    app.openSettings('backends')
    await settled()
    app.settings.installRuntime('runtime')
    expect(app.shell.sheet).toBe('runtimeConsent')
    app.video.install()
    expect(app.shell.sheet).toBeUndefined()
    // The still frame's 43 ticks play the install whole at once: it ran, and its card shows beside the run, still going.
    expect(app.child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments.slice(0, 2).join(' '))).toEqual(['video generate', 'train setup'])
    expect(app.video.face().install.shown).toBe(true)
    expect(app.video.face().run).toMatchObject({ shown: true, live: true })
    expect(app.placeRuns.video).toBe(1)
  })

  test('⌘6 opens Video through the key map, which reads the folder and the store as it comes into view; the pipeline line opens Models’ Stored weights at the default', async () => {
    const app = await started('chat')
    app.shell.press(chord('6', 'command'))
    expect(app.shell.route).toBe('video')
    await settled()
    expect(app.video.face().list.map((row) => row.file)).toEqual(['lighthouse-dusk-2.mp4', 'lighthouse-dusk.mp4', 'lighthouse-dusk-draft.mp4', 'harbor-first-light.mp4', 'rain-window.mp4'])
    app.video.openDefault()
    expect(app.shell.route).toBe('models')
    expect(app.models.tab).toBe('store')
  })
})

describe('the compositions in the composition (design 1l)', () => {
  const runRows = (app: AppViewModel) => app.monitor.face().models.rows.filter((row) => row.run !== undefined)

  test('compose: Video on Compositions, counted from its projects; the heat pump paused 9.6 s in on its caption, Make changes beside it, nothing said', async () => {
    const app = await started('compose')
    expect(app.shell.route).toBe('video')
    expect(app.video.face().tabs.map((tab) => `${tab.label} ${tab.count}`)).toEqual(['Clips 5', 'Compositions 4'])
    expect(app.video.face().tab).toBe('compositions')
    const page = app.compose.face().page
    expect([page.title, page.status, page.player.time, page.player.caption]).toEqual(['How a heat pump works', 'rendered', '0:09.6 / 0:33.5', 'A heat pump gathers it with a refrigerant that runs colder than the air itself.'])
    expect(app.compose.face().edit.locked).toBe(false)
    expect(app.shell.toast).toBeUndefined()
    expect(app.child.unanswered).toEqual([])
  })

  test('compose:edit: Tides, made, its recorded edit previewed by the dry run the CLI recorded; the list five', async () => {
    const app = await started('compose:edit')
    const edit = app.compose.face().edit
    expect([edit.shown, edit.text, edit.head]).toEqual([true, 'make the second scene the moon', 'the edit: 2 scenes, were 2'])
    expect(edit.diff.map((row) => row.text)).toEqual(['scene 1, untouched', 'scene 2; its prompt changed'])
    expect(app.video.face().tabs[1]?.count).toBe('5')
    expect(app.placeRuns.video).toBe(0)
  })

  test('compose:new: a new composition, the recorded brief planned, nothing rendered -- a plan on paper is no run the badge counts', async () => {
    const app = await started('compose:new')
    const face = app.compose.face()
    expect([face.isNew, face.draft.meta, face.fresh.title, face.fresh.planLabel, face.paneLabel]).toEqual([true, 'planned · nothing rendered', 'Tides', 'Plan again', 'Composition settings'])
    expect(app.shell.toast?.text).toBe('Planned the-tides-twice-a-day.mp4: nothing is rendered yet -- Render it, on its page, renders the plan')
    expect(app.video.face().tabs[1]?.count).toBe('4')
    // A plan on paper running (its job registered as the job starter registers it): no run of the badge's, nor the monitor's; a render is.
    const plan = app.registry.register('job', 'apogee video compose the tides, twice a day', async () => {}, ['video', 'compose', '-o', '/Users/someone/Movies/Apogee/the-tides-twice-a-day.mp4', '--dry-run', '--output-format', 'stream-json', '--', 'the tides, twice a day'])
    expect(app.placeRuns.video).toBe(0)
    expect(runRows(app)).toEqual([])
    plan.leave()
    app.registry.register('job', 'apogee video compose the tides, twice a day', async () => {}, ['video', 'compose', '-o', '/Users/someone/Movies/Apogee/the-tides-twice-a-day.mp4', '--output-format', 'stream-json', '--', 'the tides, twice a day'])
    expect(app.placeRuns.video).toBe(1)
  })

  test('compose:tide: the tide project, stopped at scene 5 -- Resume renders it as a job on the shared list: the sidebar counts it, the monitor names it, its Open there shows it on Compositions, and its page’s Stop stops it, the finished scenes kept', async () => {
    const app = await started('compose:tide')
    expect(app.compose.face().page).toMatchObject({ title: 'Tide pools for kids', status: 'stopped', stopped: true })
    const tide = app.compose.selectedProject
    if (tide) app.compose.resume(tide)
    expect(app.placeRuns.video).toBe(1)
    // The monitor open over another place, another project shown: its Open goes to the render, as the old crender row does.
    app.compose.select('/Users/someone/Movies/Apogee/heat-pump.mp4')
    app.video.pick('clips')
    app.shell.go('sessions')
    app.shell.open('monitor')
    const row = runRows(app)[0]
    expect(row).toMatchObject({ name: 'tide-pools.mp4 · rendering', action: 'Open' })
    if (row) app.monitor.act(row)
    expect([app.shell.overlay, app.shell.route, app.video.face().tab, app.compose.selected]).toEqual([undefined, 'video', 'compositions', '/Users/someone/Movies/Apogee/tide-pools.mp4'])
    expect(app.placeRuns.video).toBe(1)
    await app.compose.stop()
    await settled()
    expect(app.placeRuns.video).toBe(0)
    expect(app.compose.face().page).toMatchObject({ status: 'stopped', stopped: true })
    expect(app.compose.selectedProject?.status).toBe('cancelled')
  })

  test('compose:differs: the Tides edit previewed, then rendered: its render planned again and came out otherwise, said above its changes', async () => {
    const app = await started('compose:differs')
    await settled()
    const page = app.compose.face().page
    expect([page.title, page.tag, page.status, page.differs]).toEqual(['Tides', 'EDIT 1', 'rendered', 'The render planned again, and its changes differ from the preview'])
    expect(app.compose.face().list.map((row) => row.file)).toEqual(['tides-edit.mp4', 'heat-pump.mp4', 'night-market-2.mp4', 'tide-pools.mp4', 'night-market.mp4', 'tides.mp4'])
    expect(app.video.face().tabs[1]?.count).toBe('6')
    expect(app.child.unanswered).toEqual([])
  })

  test('Video’s tabs switch between the clips and the compositions; New composition there opens the draft, and the window’s chosen voice is where it starts', async () => {
    const app = await started('video')
    expect(app.video.face().newLabel).toBe('New clip')
    app.video.pick('compositions')
    expect(app.video.face()).toMatchObject({ tab: 'compositions', newLabel: 'New composition', canNew: true })
    app.settings.setVoice('bm_george')
    app.video.newPrimary()
    expect(app.compose.face().isNew).toBe(true)
    expect(app.compose.face().fresh.voice).toBe('bm_george')
    app.video.pick('clips')
    expect(app.video.face().tab).toBe('clips')
  })

  test('Settings’ Caption new compositions reaches a new composition: unticked there, the draft starts with captions off and --no-captions (the old freshCompose’s mediaCaptions)', async () => {
    const app = await started('settings:media')
    app.settings.toggleCaptions()
    expect(app.settings.face().media.captions).toBe(false)
    app.video.pick('compositions')
    app.video.newPrimary()
    expect(app.compose.face().fresh.captions).toBe(false)
    expect(app.compose.face().fresh.command).toMatch(/ --no-captions /)
  })

  test('a voice chosen in Settings leaves an open new composition its own voice; the next fresh one starts from it (the old 6256 sets the default alone)', async () => {
    const app = await started('video')
    app.video.pick('compositions')
    app.video.newPrimary()
    expect(app.compose.face().fresh.voice).toBe('af_heart')
    app.settings.setVoice('bm_george')
    expect(app.compose.face().fresh.voice).toBe('af_heart')
    expect(app.compose.face().fresh.command).not.toContain('--voice bm_george')
  })
})

describe('the Voice place in the composition (design 1m)', () => {
  test('voice: the two recorded speeches, speech-torch.json shown, its player paused on its second sentence, the old sample in the card; nothing said, nothing unanswered', async () => {
    const app = await started('voice')
    expect(app.shell.route).toBe('voice')
    const face = app.voice.face()
    expect(face.modelLine).toBe('Kokoro-82M · PyTorch')
    expect(face.list.map((row) => row.file)).toEqual(['hello-mlx.wav', 'hello-pytorch.wav'])
    expect(face.speech).toMatchObject({ shown: true, file: 'hello-pytorch.wav' })
    expect(face.speech.player).toMatchObject({ playing: false, time: '0:00.1 / 0:00.1', pct: 60 })
    expect(app.voice.currentSentence).toBe(1)
    expect(face.card).toMatchObject({ count: '118 / 4,000', out: 'apogee-runs-every.wav', cantSpeak: false })
    expect(face.card.groups.map((group) => group.title)).toEqual(['AMERICAN ENGLISH · 2', 'BRITISH ENGLISH · 1'])
    expect(app.shell.toast).toBeUndefined()
    expect(app.child.unanswered).toEqual([])
  })

  test('Speak runs the hand-made stream end to end: a job on the shared list -- the ⌘7 badge 1 while it speaks, the monitor’s Kokoro-82M · speaking row -- then the new speech selected and the toast', async () => {
    const app = new AppViewModel(presetFor({ start: 'voice', still: false, apogee: 'installed' }).preset, modeRules, undefined, sessionSamples)
    await app.start()
    await settled()
    app.stop()
    app.voice.speak()
    expect(app.placeRuns).toEqual({ models: 0, tasks: 0, training: 0, video: 0, voice: 1 })
    for (let tick = 0; tick < 3; tick += 1) {
      app.clock.advance(1)
      await settled()
    }
    const row = app.monitor.face().models.rows.find((candidate) => candidate.run !== undefined)
    expect(row).toMatchObject({ name: 'Kokoro-82M · speaking', detail: 'a voice run this app supervises · cpu · the CLI reports no size for it', action: 'Stop' })
    for (let tick = 0; tick < 3; tick += 1) {
      app.clock.advance(1)
      await settled()
    }
    await settled()
    expect(app.placeRuns.voice).toBe(0)
    expect(app.voice.selected).toBe('/Users/someone/Music/Apogee/apogee-runs-every.wav')
    expect(app.voice.face().list.map((row) => row.file)).toEqual(['apogee-runs-every.wav', 'hello-mlx.wav', 'hello-pytorch.wav'])
    expect(app.shell.toast?.text).toBe('apogee-runs-every.wav is ready · 0.1 s, measured from its header')
    expect(app.child.unanswered).toEqual([])
  })

  test('the monitor’s Stop on a speech stops it: nothing written, said in the CLI’s words', async () => {
    const app = await started('voice')
    app.voice.speak()
    const row = app.monitor.face().models.rows.find((candidate) => candidate.run !== undefined)
    expect(row?.name).toBe('Kokoro-82M · speaking')
    if (row) app.monitor.act(row)
    await settled()
    expect(app.placeRuns.voice).toBe(0)
    expect(app.shell.toast?.text).toBe('cancelled -- nothing was written (/Users/someone/Music/Apogee/apogee-runs-every.wav untouched)')
  })

  test('"Use this voice for compositions" is the window’s one MediaPreferences: Settings shows it, an open new composition takes it; Settings’ own choice leaves the card its voice', async () => {
    const app = await started('voice')
    app.video.pick('compositions')
    app.video.newPrimary()
    expect(app.compose.face().fresh.voice).toBe('af_heart')
    app.compose.editDraft({ voice: 'bm_george' })
    app.voice.pickVoice('bf_emma')
    app.settings.setVoice('am_adam')
    expect(app.voice.voice).toBe('bf_emma')
    expect(app.compose.face().fresh.voice).toBe('bm_george')
    app.voice.select('/Users/someone/Music/Apogee/hello-pytorch.wav')
    app.voice.act('compositions')
    expect(app.media.voice).toBe('af_heart')
    expect(app.compose.face().fresh.voice).toBe('af_heart')
    expect(app.settings.face().media.voice).toBe('af_heart')
    app.voice.newSpeech()
    expect(app.voice.voice).toBe('af_heart')
  })

  test('⌘7 opens Voice through the key map, which reads the folder and the store as it comes into view; the model line opens Models’ Stored weights at it', async () => {
    const app = await started('video')
    app.shell.press(chord('7', 'command'))
    expect(app.shell.route).toBe('voice')
    await settled()
    expect(app.voice.face().list.map((row) => row.file)).toEqual(['hello-mlx.wav', 'hello-pytorch.wav'])
    expect(app.voice.face().card.text).toBe('Apogee runs every model on this Mac unless you pick a cloud one. When it does use the cloud, it tells you, every time.')
    app.voice.openModel()
    expect(app.shell.route).toBe('models')
    expect(app.models.tab).toBe('store')
  })
})

describe('Library and Review in the composition (design 1n)', () => {
  test('library: the agents tab, the CLI’s eight agents, triage’s page by its name; the Streaming session behind it, nothing said or unanswered', async () => {
    const app = await started('library')
    expect(app.shell.route).toBe('library')
    const face = app.definitions.face()
    expect([face.tabs.find((tab) => tab.current)?.tab, face.rows.length, face.agent.name, face.agent.files]).toEqual(['agents', 8, 'triage', ['prompts/triage.txt', 'schemas/triage-output.json']])
    expect([app.shell.sheet, app.shell.confirm, app.shell.toast]).toEqual([undefined, undefined, undefined])
    expect(app.session.face().empty.launched).toBe(true)
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('library:symphonies: critique-revise played by the recorded play within the frame’s ticks, its answers under Play', async () => {
    const app = await started('library:symphonies')
    const page = app.definitions.face().symphony
    expect([page.name, page.playing.shown, page.played.shown, page.played.output]).toEqual(['critique-revise', false, true, 'A tighter draft, for new contributors: what the tool does, then how to run it.'])
    expect(app.child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments.slice(0, 3).join(' '))).toEqual(['symphonies play critique-revise'])
  })

  test('library:mcp: the server code and the presets the old page lists, from mcp-presets.json', async () => {
    const app = await started('library:mcp')
    const page = app.definitions.face().mcp
    expect([page.name, page.presets.map((preset) => preset.name)]).toEqual(['code', ['code-exec', 'self']])
  })

  test('library:unsaved: the triage prompt open in the shell’s sheet host with a line typed, Close asked in the confirm host; Keep editing keeps it, Discard closes both', async () => {
    const app = await started('library:unsaved')
    expect([app.shell.sheet, app.shell.confirm]).toEqual(['editor', 'editorClose'])
    expect(app.definitions.confirmWords('editorClose')?.title).toBe('Discard your changes to triage.txt?')
    expect(app.definitions.editorFace().text.endsWith('\nFile each issue under one area.\n')).toBe(true)
    app.dismissDefinitions('editorClose')
    expect([app.shell.sheet, app.shell.confirm, app.definitions.editorFace().shown]).toEqual(['editor', undefined, true])
    app.definitions.requestClose()
    expect(app.shell.confirm).toBe('editorClose')
    await app.confirmDefinitions('editorClose', false)
    expect([app.shell.sheet, app.shell.confirm, app.definitions.editorFace().shown]).toEqual([undefined, undefined, false])
  })

  test('library:refused: notes-digest saved with the unknown key -- 4 ticks, then apogee’s refusal -- Close asked with its three ways; Put it back as it was closes once apogee has read the original', async () => {
    const app = await started('library:refused')
    expect([app.shell.sheet, app.shell.confirm]).toEqual(['editor', 'editorClose'])
    expect(app.definitions.editorFace().verdict).toEqual({ text: "apogee symphonies: /Users/someone/.apogee/symphonies/notes-digest.yaml: stages[1]: unknown key 'temprature' (accepted: name, role, play, prompt, input, schema, image, brief_tokens, answer_tokens) (exited with status 1)", refused: true })
    expect(app.definitions.confirmWords('editorClose')).toMatchObject({ title: 'notes-digest.yaml holds the text apogee refused', cancel: 'Keep editing', alternative: 'Leave it', confirm: 'Put it back as it was' })
    const putBack = app.confirmDefinitions('editorClose', false)
    await settled()
    expect([app.shell.confirm, app.definitions.editorFace().saving]).toEqual([undefined, true])
    app.clock.advance(4)
    await putBack
    await settled()
    expect([app.shell.sheet, app.definitions.editorFace().shown]).toEqual([undefined, false])
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('library:refused: Keep editing, then Save the same text -- 4 ticks, refused again, as the old save judges the text -- and Close asks again', async () => {
    const app = await started('library:refused')
    const refusal = app.definitions.editorFace().verdict
    app.dismissDefinitions('editorClose')
    const saved = app.definitions.save()
    app.clock.advance(4)
    await saved
    await settled()
    expect([app.definitions.editorFace().verdict, refusal.refused]).toEqual([refusal, true])
    app.definitions.requestClose()
    expect([app.shell.sheet, app.shell.confirm]).toEqual(['editor', 'editorClose'])
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('library:image: describe-answer shown, no image chosen: Play waits and says why', async () => {
    const app = await started('library:image')
    const page = app.definitions.face().symphony
    expect([page.name, page.image.chosen, page.canPlay, page.needsImage]).toEqual(['describe-answer', '', false, 'describe-answer takes an image with its input: choose one to play it.'])
  })

  test('review: the recorded dry run of acme/widget#12 on the Reviews tab, played within the frame’s ticks; review:post asks to post in the shell’s confirm host, and Post closes it, running nothing', async () => {
    const review = await started('review')
    expect([review.shell.route, review.review.face().runs.run.title, review.shell.confirm]).toEqual(['review', 'Thumbnails', undefined])
    const app = await started('review:post')
    expect(app.shell.confirm).toBe('reviewPost')
    expect(app.review.postWords?.title).toBe('Post the review to github.com/acme/widget#12?')
    const jobs = app.child.calls.filter((call) => call.kind === 'jobs').length
    app.confirmReviewPost()
    expect([app.shell.confirm, app.review.postWords, app.child.calls.filter((call) => call.kind === 'jobs').length]).toEqual([undefined, undefined, jobs])
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('review:targets and review:skills: their tabs, the targets with the kept sweep, the lenses with security shown; Add target… opens the sheet host, and Add puts the row on the tab', async () => {
    const skills = await started('review:skills')
    expect([skills.review.face().skills.skill.name, skills.review.face().skills.rows.length]).toEqual(['security', 7])
    const app = await started('review:targets')
    expect([app.review.face().targets.rows.length, app.review.face().targets.sweep.shown]).toEqual([2, true])
    app.review.newTarget()
    expect(app.shell.sheet).toBe('target')
    await app.review.addTarget()
    expect([app.shell.sheet, app.review.face().targets.rows.map((row) => row.name), app.shell.toast?.text]).toEqual([undefined, ['acme-all', 'widget', 'gadget'], "added review target 'gadget' to /Users/someone/.apogee/config/config.json"])
  })

  test('the no-apogee and config-typo notes on both screens, in the recorded words', async () => {
    const missing = await started('library', 'missing')
    expect(missing.definitions.face().note).toBe("Apogee isn't installed, so there are no agents to list.")
    const missingReview = await started('review:targets', 'missing')
    expect(missingReview.review.face().targets.note).toBe("Apogee isn't installed, so there is nothing to review with.")
    const typo = await started('library:mcp', 'config-typo')
    expect(typo.definitions.face().note.startsWith('The MCP servers could not be read: apogee mcp: /Users/someone/.apogee/config/config.json: not valid JSON')).toBe(true)
    const typoReview = await started('review:targets', 'config-typo')
    expect(typoReview.review.face().targets.note.startsWith('The targets could not be read: apogee review: /Users/someone/.apogee/config/config.json: not valid JSON')).toBe(true)
  })

  test('⌘0 opens Library and ⇧⌘R Review through the key map, each read as it comes into view (the browser may keep ⌘0 for itself)', async () => {
    const app = await started('chat:new')
    app.shell.press(chord('0', 'command'))
    expect(app.shell.route).toBe('library')
    await settled()
    expect(app.definitions.face().agent.name).toBe('triage')
    app.shell.press(chord('r', 'shift', 'command'))
    expect(app.shell.route).toBe('review')
    await settled()
    await settled()
    expect(app.review.face().skills.rows.length).toBe(7)
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('MCP Remove… from the confirm host: the CLI’s line in the toast, the server gone from the list', async () => {
    const app = await started('library:mcp')
    app.definitions.mcp.askToRemove()
    expect(app.shell.confirm).toBe('mcpRemove')
    await app.confirmDefinitions('mcpRemove', false)
    await settled()
    expect([app.shell.confirm, app.definitions.face().rows, app.shell.toast?.text]).toEqual([undefined, [], "removed MCP server 'code' from /Users/someone/.apogee/config/config.json"])
  })
})

describe('Knowledge and the Code graph in the composition (design 1o)', () => {
  test('knowledge: Records, the old question asked and answered by knowledge-query.json, the rejected record open; the Streaming session behind it, nothing said or unanswered', async () => {
    const app = await started('knowledge')
    expect([app.shell.route, app.knowledgeTab]).toEqual(['knowledge', 'records'])
    const face = app.records.face()
    expect([face.question, face.found, face.rows.map((row) => row.score), face.record.id, face.record.raw.shown]).toEqual(['why was the cancel button removed', '3 found · lexical', ['0.596', '0.000', '0.000'], 'kr-20261021T045944Z-1e2354', true])
    expect([app.shell.sheet, app.shell.confirm, app.shell.toast]).toEqual([undefined, undefined, undefined])
    expect(app.session.face().empty.launched).toBe(true)
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('knowledge:collections and knowledge:mixed: Collections, notes searched in collection-query.txt’s lines; mixed and its Vectors line, nothing searched', async () => {
    const searched = await started('knowledge:collections')
    expect([searched.knowledgeTab, searched.collections.face().page.name, searched.collections.face().page.searched, searched.collections.face().page.results.length]).toEqual(['collections', 'notes', true, 10])
    const mixed = await started('knowledge:mixed')
    const page = mixed.collections.face().page
    expect([mixed.knowledgeTab, page.name, page.searched, page.vectors]).toEqual(['collections', 'mixed', false, "4 of 4 chunks, 8-d [mixed: 'mock-embed' and 'mock-embed-b' -- re-ingest every source with one model]"])
    expect(mixed.collections.face().rows.map((row) => row.current)).toEqual([false, true, false])
    mixed.pickKnowledgeTab('records')
    expect(mixed.knowledgeTab).toBe('records')
  })

  test('graph and graph:prose: the Functions view on the recorded demo graph, Runner.run_batch open; the notes prose graph', async () => {
    const code = await started('graph')
    expect([code.shell.route, code.graph.view, code.graph.face().code.card.label, code.graph.face().header.line]).toEqual(['graph', 'functions', 'Runner.run_batch', '~/src · 4 files · 16 symbols'])
    expect([...code.child.unanswered, ...code.consoleChild.unanswered]).toEqual([])
    const prose = await started('graph:prose')
    expect([prose.graph.view, prose.graph.face().prose.summary.startsWith('Graph "notes" holds 4 entities'), prose.graph.face().prose.entity.name]).toEqual(['prose', true, 'Heat pump'])
  })

  test('graph:refused: Runner._fetch clicked in an overtaken picture, its explain refused in the shell’s confirm host over the kept canvas; OK clears the selection', async () => {
    const app = await started('graph:refused')
    expect([app.shell.route, app.shell.confirm, app.graph.confirmWords('explainRefused')?.body.startsWith("apogee graph: no node named 'ingest.runner.Runner._fetch' in graph 'demo'")]).toEqual(['graph', 'explainRefused', true])
    expect([app.graph.face().code.shown, app.graph.face().code.card.shown]).toEqual([true, false])
    app.dismissGraph('explainRefused')
    expect([app.shell.confirm, app.graph.selected, app.graph.face().code.nodes.length]).toEqual([undefined, undefined, 6])
    // Only Runner._fetch is refused, as the old pickNode refuses it alone: the place's opening node shows its card, no alert.
    await app.graph.select('ingest.runner.Runner.run_batch')
    expect([app.shell.confirm, app.graph.face().code.card.shown, app.graph.face().code.card.label]).toEqual([undefined, true, 'Runner.run_batch'])
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('the More menu opens the shell’s scrim, which closes it; the save panel and dedupe sheet in its sheet host; delete through its confirm host', async () => {
    const app = await started('graph')
    app.graph.toggleMore()
    expect(app.shell.menu).toBe('graphMore')
    app.shell.closeMenus()
    expect(app.graph.moreOpen).toBe(false)
    app.graph.exportAs('html')
    expect(app.shell.sheet).toBe('savePanel')
    await app.graph.save()
    expect([app.shell.sheet, app.shell.toast?.text]).toEqual([undefined, 'Exported graph "demo" to /Users/someone/Documents/demo.html: 9 entities and 12 relations drawn, 41 KB.'])
    await app.graph.planDedupe()
    expect(app.shell.sheet).toBe('dedupe')
    app.graph.dismissDedupe()
    expect(app.shell.sheet).toBe(undefined)
    app.graph.askToDelete()
    expect(app.shell.confirm).toBe('graphDelete')
    app.dismissGraph('graphDelete')
    expect([app.shell.confirm, app.graph.pendingDelete]).toEqual([undefined, undefined])
    app.graph.askToDelete()
    await app.confirmGraph('graphDelete')
    expect([app.shell.confirm, app.graph.face().gone.shown]).toEqual([undefined, true])
  })

  test('Ask about this opens a new session on Sessions, the node’s file attached as a chip and the old draft in the composer', async () => {
    const app = await started('graph')
    app.graph.askAboutThis()
    await settled()
    app.clock.advance(3)
    await settled()
    await settled()
    expect(app.shell.route).toBe('sessions')
    const face = app.session.face()
    expect([face.composer.draft, face.composer.attachments]).toEqual(['Explain Runner.run_batch and what depends on it.', ['/Users/someone/src/ingest/runner.py']])
    expect(app.child.calls.filter((call) => call.kind === 'chats').length).toBe(2)
  })

  test('⌘5 opens the Code graph and ⌘8 Knowledge through the key map, each read as it comes into view (the browser may keep ⌘1-⌘9 for its tabs)', async () => {
    const app = await started('chat:new')
    app.shell.press(chord('5', 'command'))
    expect(app.shell.route).toBe('graph')
    await settled()
    await settled()
    expect(app.graph.face().code.nodes.length).toBe(6)
    app.shell.press(chord('8', 'command'))
    expect(app.shell.route).toBe('knowledge')
    await settled()
    await settled()
    expect([app.records.face().rows.length, app.collections.face().rows.length]).toEqual([3, 3])
    // Ask and Search with nothing typed ask the old questions, as kno.ask and kno.search do.
    await app.records.ask()
    await app.collections.search()
    expect([app.records.face().question, app.records.face().found, app.collections.face().page.question, app.collections.face().page.results.length]).toEqual(['why was the cancel button removed', '3 found · lexical', 'how does a heat pump work', 10])
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('the no-apogee state of both places draws no words, as the old has none; config-typo says the CLI’s refusal', async () => {
    const missing = await started('graph', 'missing')
    expect([missing.graph.face().gone, missing.graph.face().more.enabled]).toEqual([{ shown: true, text: '' }, false])
    const knowledge = await started('knowledge', 'missing')
    expect([knowledge.records.face().empty, knowledge.records.face().rows, knowledge.collections.face().note, knowledge.collections.face().rows]).toEqual(['', [], '', []])
    const typo = await started('knowledge', 'config-typo')
    expect(typo.records.face().empty.startsWith('The records could not be read: apogee knowledge: ')).toBe(true)
  })
})

describe('Suites and Tasks in the composition (design 1p)', () => {
  test('suites: the recorded five, the default (fast) chosen; the Streaming session behind it, nothing said or unanswered', async () => {
    const app = await started('suites')
    expect(app.shell.route).toBe('suites')
    expect(app.suites.face().list.map((row) => row.name)).toEqual(['cloud', 'fast', 'pair', 'shared', 'vast'])
    expect(app.suites.face().detail.name).toBe('fast')
    expect(app.shell.toast).toBeUndefined()
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('Open a chat under a suite opens a new chat under it on Sessions, forced where it does not fit, through the app root’s leave', async () => {
    const app = await started('suites')
    app.suites.select('vast')
    app.suites.openChatUnderIt()
    await settled()
    await settled()
    expect(app.shell.route).toBe('sessions')
    expect(app.child.calls.filter((call) => call.kind === 'chats').at(-1)?.arguments).toEqual(expect.arrayContaining(['--suite', 'vast', '--force']))
  })

  test('Open a chat under a suite asks the endPrivate confirm while a private session has a turn sent, and goes on once End it and switch is pressed', async () => {
    const app = await started('chat:private')
    app.session.setDraft('a secret')
    app.session.submit()
    app.shell.go('suites')
    await app.suites.appeared()
    expect(app.suites.face().detail.canSwitch, 'a private session with a turn sent cannot move').toBe(false)
    app.suites.openChatUnderIt()
    expect(app.shell.confirm).toBe('endPrivate')
    expect(app.shell.route).toBe('suites')
    app.endPrivateAndLeave()
    await settled()
    await settled()
    expect(app.shell.route).toBe('sessions')
    expect(app.child.calls.filter((call) => call.kind === 'chats').at(-1)?.arguments).toEqual(expect.arrayContaining(['--suite', 'fast']))
  })

  test('Switch the session to it moves the open session under the suite and shows Sessions', async () => {
    const app = await started('chat:new')
    app.shell.go('suites')
    await app.suites.appeared()
    app.suites.select('pair')
    app.suites.switchSessionToIt()
    await settled()
    await settled()
    expect(app.shell.route).toBe('sessions')
    expect(app.child.calls.filter((call) => call.kind === 'chats').at(-1)?.arguments).toEqual(expect.arrayContaining(['--suite', 'pair']))
  })

  test('Switch the session to it anyway moves the open session under a suite that does not fit, forced', async () => {
    const app = await started('chat:new')
    app.shell.go('suites')
    await app.suites.appeared()
    app.suites.select('vast')
    app.suites.switchSessionToIt()
    await settled()
    await settled()
    expect(app.shell.route).toBe('sessions')
    expect(app.child.calls.filter((call) => call.kind === 'chats').at(-1)?.arguments).toEqual(expect.arrayContaining(['--suite', 'vast', '--force']))
  })

  test('Use for new sessions runs config set-default-suite and has the Models place read its roles again', async () => {
    const app = await started('suites')
    app.suites.select('cloud')
    const before = app.child.calls.length
    await app.suites.makeDefault()
    await settled()
    const after = app.child.calls.slice(before).map((call) => call.arguments.join(' '))
    expect(after).toContain('config set-default-suite cloud')
    expect(after.filter((call) => call === 'models status').length).toBeGreaterThanOrEqual(2)
    expect(app.shell.toast?.text).toBe('cloud is the default suite')
  })

  test('tasks: the live recorded run in round 1, shown, the sidebar’s Tasks badge 1 while it runs; the badge empty once it ends', async () => {
    const app = await started('tasks')
    expect(app.shell.route).toBe('tasks')
    expect(app.tasks.face().detail).toMatchObject({ goal: 'say hello', status: 'running · round 1 of 2', canPause: true, canStop: true })
    expect(app.placeRuns.tasks).toBe(1)
    app.clock.advance(20)
    await settled()
    await settled()
    expect(app.placeRuns.tasks).toBe(0)
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('tasks: Stop after this round and Stop now reach the live run through the app’s one stand-in -- the job its reads’ command names hears it -- halted after round 1, or cancelled in it', async () => {
    const halted = await started('tasks')
    await halted.tasks.halt()
    expect(halted.tasks.face().detail.status).toBe('running · halts when round 1 ends')
    halted.clock.advance(10)
    await settled()
    await settled()
    expect(halted.tasks.face().detail).toMatchObject({ status: 'halted · resumes from its ledger', canResume: true })
    expect(halted.tasks.face().detail.rounds.map((each) => each.n)).toEqual(['Plan', 'Round 1'])
    const cancelled = await started('tasks')
    await cancelled.tasks.cancel()
    cancelled.clock.advance(4)
    await settled()
    await settled()
    expect(cancelled.tasks.face().detail.status).toBe('cancelled by you')
    expect([...halted.child.unanswered, ...cancelled.child.unanswered]).toEqual([])
  })

  test('no other preset runs a task: the Tasks badge reads the shared job list, and nothing there is a task', async () => {
    for (const start of ['chat', 'models', 'suites', 'tasks:resumerefused']) expect((await started(start)).placeRuns.tasks, start).toBe(0)
  })

  test('tasks:denied: the recorded uses run played to its end, shown: Allowed and Denied, and Run again', async () => {
    const app = await started('tasks:denied')
    const detail = app.tasks.face().detail
    expect([detail.goal, detail.status, detail.allowed, detail.denied, detail.rerunLabel]).toEqual([
      'Write the report',
      'exhausted · 1 of 1 rounds used',
      ["round 1: run_command on ls -- this task's grant"],
      ['round 1: write_file on report.txt -- nobody present to allow it'],
      'Run again with 2 rounds',
    ])
  })

  test('tasks:more: the newest 50 of the ledger of 60 and Show all, the live run above them', async () => {
    const app = await started('tasks:more')
    expect([app.tasks.face().list.length, app.tasks.face().truncation]).toEqual([51, 'The newest 50 of 60 tasks'])
  })

  test('tasks:resuming: the stalled task’s resume starting up -- Resuming… and the stops; tasks:resumerefused: the refusal in the shell’s confirm host, its line kept on the page, OK clearing the alert', async () => {
    const resuming = await started('tasks:resuming')
    expect(resuming.tasks.face().detail).toMatchObject({ goal: 'Find the word', isResuming: true, canPause: true, canStop: true })
    const refused = await started('tasks:resumerefused')
    expect(refused.shell.confirm).toBe('taskRefused')
    expect(refused.tasks.face().detail.refused.startsWith('apogee task: task task-20261109-021236 cannot resume')).toBe(true)
    refused.confirmTasks()
    expect([refused.shell.confirm, refused.tasks.failure]).toEqual([undefined, undefined])
  })

  test('tasks:refused: the sheet’s start refused, no row left, the alert and the toast; newtask and newtask:recipe: the sheet open in the shell’s host, filled as the old frames fill it', async () => {
    const refused = await started('tasks:refused')
    expect(refused.shell.confirm).toBe('taskRefused')
    expect(refused.shell.sheet).toBeUndefined()
    expect(refused.shell.toast?.text.startsWith("That didn't work: apogee task: --allow write_file")).toBe(true)
    expect(refused.tasks.face().list.length).toBe(4)
    const goal = await started('newtask')
    expect(goal.shell.sheet).toBe('newtask')
    expect(goal.tasks.sheetFace().command).toBe('apogee task run "Make tests/test_parsers.py pass on Python 3.12" --tools --allow write_file --allow run_command --suite fast --output-format stream-json')
    const recipe = await started('newtask:recipe')
    expect(recipe.tasks.sheetFace().command).toBe('apogee task run --recipe digest-folder --input docs --allow write_file --suite fast --output-format stream-json')
    recipe.tasks.cancelNew()
    expect(recipe.shell.sheet).toBeUndefined()
  })

  test('⌘3 opens Suites and ⌘4 Tasks through the key map, each read as it comes into view (the browser may keep ⌘1-⌘9 for its tabs)', async () => {
    const app = await started('chat:new')
    app.shell.press(chord('3', 'command'))
    expect(app.shell.route).toBe('suites')
    await settled()
    await settled()
    expect(app.suites.face().list.length).toBe(5)
    app.shell.press(chord('4', 'command'))
    expect(app.shell.route).toBe('tasks')
    await settled()
    await settled()
    expect(app.tasks.face().list.map((row) => row.goal)).toEqual(['Answer when asked', 'say hello', 'say hello'])
    expect([...app.child.unanswered, ...app.consoleChild.unanswered]).toEqual([])
  })

  test('the no-apogee state of both places draws no words, as the old has none; config-typo says the CLI’s refusal', async () => {
    const missing = await started('tasks', 'missing')
    expect([missing.tasks.face().list, missing.tasks.face().empty, missing.tasks.face().detail.shown]).toEqual([[], '', false])
    const suites = await started('suites', 'missing')
    expect([suites.suites.face().list, suites.suites.face().empty]).toEqual([[], ''])
    const typo = await started('tasks', 'config-typo')
    expect(typo.tasks.face().empty.startsWith('The tasks could not be read: apogee task: ')).toBe(true)
  })
})
