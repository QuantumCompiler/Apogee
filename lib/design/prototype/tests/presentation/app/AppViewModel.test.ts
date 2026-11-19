import { describe, expect, test } from 'vitest'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel } from '../../../source/presentation/app/ViewModels/AppViewModel'
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

  test('opening the file with no start shows the monitor, until 1v restores chat', async () => {
    const app = await started(undefined)
    expect(app.preset.route).toBe('monitor')
    expect(app.shell.overlay).toBe('monitor')
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
    expect(app.child.calls).toHaveLength(4)
  })
})

describe('the preset table', () => {
  test('monitor is known, and an unknown preset opens the default, saying it was not known', () => {
    expect(presetFor({ start: 'monitor', still: true, apogee: 'installed' }).known).toBe(true)
    const unknown = presetFor({ start: 'no-such-screen', still: false, apogee: 'missing' })
    expect(unknown.known).toBe(false)
    expect(unknown.preset.route).toBe('monitor')
    expect(unknown.preset.apogee).toBe('missing')
    expect(unknown.preset.clock.still).toBe(false)
  })
})

describe('the session in the composition (design 1u)', () => {
  test('under the monitor, Sessions is the session window’s empty state, and nothing is launched', async () => {
    const app = await started('monitor')
    expect(app.session.face().state).toBe('empty')
    expect(app.session.connection).toEqual({ case: 'idle' })
    expect(app.child.calls.map((call) => call.kind)).toEqual(['reads', 'reads', 'reads', 'reads'])
    expect(app.child.calls.every((call) => call.arguments[0] === 'system')).toBe(true)
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

  test('under the monitor the session is not launched, so the empty state offers nothing that needs one: every act leaves nothing unanswered and nothing launched', async () => {
    const app = await started('monitor')
    const session = app.session
    const face = session.face()
    expect(face.empty).toMatchObject({ launched: false, privateInstead: false, chooseSuite: false })
    expect(face.header.canSwitchSuite).toBe(false)
    expect(face.composer.canAttach).toBe(false)
    session.chooseSuite()
    session.attach('~/code/ingest/')
    await settled()
    expect(app.child.unanswered).toEqual([])
    expect(app.session).toBe(session)
    expect(session.connection).toEqual({ case: 'idle' })
    expect(session.held).toEqual([])
    expect(app.shell.sheet).toBeUndefined()
    expect(app.shell.toast).toBeUndefined()
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

test('a session replaced stops listening to the clock, launched or not: under the monitor the first was never launched', async () => {
  const app = await started('monitor')
  const listening = () => (app.clock as unknown as { listeners: unknown[] }).listeners.length
  const before = listening()
  for (let index = 0; index < 3; index += 1) {
    app.session.leave({ type: 'private' })
    await settled()
  }
  expect(listening()).toBe(before)
})
