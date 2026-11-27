import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import suitesModelsList from '@recorded/business/reads/Fixtures/suites-models-list.json?raw'
import statusFast from '@recorded/business/reads/Fixtures/suites-models-status-fast.json?raw'
import statusVast from '@recorded/business/reads/Fixtures/suites-models-status-vast.json?raw'
import codeAnswer from '@recorded/data/protocol/Fixtures/code-answer.jsonl?raw'
import executeProject from '@recorded/data/protocol/Fixtures/execute-project.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
import { describe, expect, test } from 'vitest'
import { ModelKind, ModelRow } from '../../../source/business/reads/Models'
import { SuiteFootprint } from '../../../source/business/reads/SuiteFootprint'
import { ApogeeClient } from '../../../source/business/session/ApogeeClient'
import type { ChatLaunch } from '../../../source/business/session/ChatLaunch'
import type { MachineSession } from '../../../source/business/session/MachineSession'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import type { ClockSettings } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'
import { SessionViewModel } from '../../../source/presentation/session/ViewModels/SessionViewModel'
import type { SessionAction } from '../../../source/presentation/session/ViewModels/SessionViewModel'

const settled = () => new Promise((resolve) => setTimeout(resolve, 0))

/** A session over the stand-in answering every chat with `recording`, started, its acts handed to `acts`. */
async function opened(recording: string, launch: ChatLaunch = {}, clock: ClockSettings = {}, apogee: ApogeeMode = 'installed') {
  const child = new StandInChild({ rules: { chats: [{ arguments: [launch.execute ? 'execute' : 'chat'], answer: { stdout: recording } }] }, apogee }, { configTypoRefusal: agentsListBadConfig })
  const ticks = new Clock(clock)
  const starter = new ApogeeClient(child, ticks)
  const model = new SessionViewModel(starter, ticks, launch, { folder: '~/code/ingest', files: [{ path: 'src/ingest/runner.py', size: '4.2 KB' }, { path: 'README.md', size: '5.1 KB' }] }, [
    { label: 'Folder…', hint: '⇧⌘O', path: '~/code/ingest/' },
  ])
  const acts: SessionAction[] = []
  model.onAction = (act) => acts.push(act)
  let opened: MachineSession | undefined
  const open = starter.openChat.bind(starter)
  starter.openChat = (binary, how) => {
    opened = open(binary, how) as MachineSession
    return opened
  }
  await model.start()
  return { model, clock: ticks, child, acts, written: () => opened?.written ?? [] }
}

/** Ticks the clock and lets the events it played land. */
async function tick(clock: Clock, count = 1) {
  clock.advance(count)
  await settled()
}

describe('the session view-model: connection, transcript, draft, send and stop (the Mac app’s SessionViewModel)', () => {
  test('it finds the binary and opens a chat: connected, then ready once the session speaks, with nothing sent -- the empty state', async () => {
    const { model, clock, child } = await opened(newUnsent)
    expect(model.connection).toEqual({ case: 'connected', binary: StandInChild.binary })
    expect(model.transcript.status).toEqual({ case: 'connecting' })
    expect(model.isEmpty).toBe(false)
    await tick(clock)
    expect(model.transcript.status).toEqual({ case: 'ready' })
    expect(model.isEmpty).toBe(true)
    expect(model.title).toBe('New session')
    expect(model.placeholder).toBe('Message claude')
    expect(child.calls.map((call) => call.arguments[0])).toEqual(['chat'])
  })

  test('missing: no binary is found, nothing is launched, and the window says so', async () => {
    const { model, child } = await opened(newUnsent, {}, {}, 'missing')
    expect(model.connection).toEqual({ case: 'missing' })
    expect(child.calls).toEqual([])
    expect(model.placeholder).toBe('apogee isn’t installed')
  })

  test('private-unsent.jsonl: private, the empty state, the CLI’s notice kept under it, and the input says private', async () => {
    const { model, clock } = await opened(privateUnsent, { privately: true })
    await tick(clock, 2)
    expect(model.privateMode).toBe('nothingWritten')
    expect(model.title).toBe('Private session')
    expect(model.isEmpty).toBe(true)
    expect(model.placeholder).toBe('Message claude · private')
  })

  test('markdown-answer.jsonl end to end: the pending line, the reasoning, the answer as its deltas join, then the result’s footer', async () => {
    const { model, clock, written } = await opened(markdownAnswer)
    await tick(clock)
    model.setDraft('How do I keep the batch going?')
    expect(model.canSend).toBe(true)
    model.submit()
    expect(model.draft).toBe('')
    expect(written()).toEqual(['{"type":"user","text":"How do I keep the batch going?"}'])
    expect(model.pending).toBe(true)
    expect(model.statusWord).toBe('working')
    expect(model.streaming).toBe(true)
    await tick(clock)
    expect(model.statusWord).toBe('thinking')
    expect(model.pendingSeconds).toBe('0.4')
    await tick(clock)
    expect(model.pending, 'the answer has started').toBe(false)
    await tick(clock, 10)
    const streaming = model.items.at(-1)?.content
    expect(streaming?.case === 'answer' && !streaming.answer.finished && streaming.answer.text.startsWith('## Timeouts')).toBe(true)
    await tick(clock, 200)
    const answer = model.items.at(-1)?.content
    expect(answer?.case === 'answer' && answer.answer.finished).toBe(true)
    expect(model.transcript.model).toBe('chatty')
    expect(model.streaming).toBe(false)
    expect(model.canCancel).toBe(false)
  })

  test('code-answer.jsonl replays whole: the answer drawn is the result’s text, delta for delta', async () => {
    const { model, clock } = await opened(codeAnswer)
    await tick(clock)
    model.setDraft('Show me a retry in every language you know.')
    model.submit()
    await tick(clock, 400)
    const answer = model.items.at(-1)?.content
    expect(answer?.case === 'answer' ? answer.answer.text : '').toContain('Enum.each(1..3, fn i -> IO.puts("attempt #{i}") end)')
    expect(answer?.case === 'answer' ? answer.answer.text.endsWith('```') : false).toBe(true)
  })

  test('stopped-turn.jsonl: Stop writes a cancel only while a turn is in flight, and the turn ends stopped', async () => {
    const { model, clock, written } = await opened(stoppedTurn)
    await tick(clock)
    model.setDraft('Which port does the dev server use?')
    model.submit()
    await tick(clock, 20)
    model.cancel()
    expect(written().filter((line) => line.includes('cancel')), 'no turn in flight: no cancel').toEqual([])
    model.setDraft('When is the launch?')
    model.submit()
    await tick(clock, 2)
    expect(model.canCancel).toBe(true)
    model.cancel()
    expect(written().at(-1)).toBe('{"type":"cancel"}')
    expect(model.statusWord).toBe('stopping')
    await tick(clock, 5)
    expect(model.items.at(-1)?.content).toEqual({ case: 'stopped' })
    expect(model.streaming).toBe(false)
  })

  test('a still preset’s send, written before the clock starts: drawn after the launch’s notices, its turn played by the start ticks', async () => {
    const { model, clock } = await opened(executeProject, { execute: {} }, { start: 20, still: true })
    model.writeAhead('hi')
    clock.start()
    await settled()
    const said = model.items.map((item) => (item.content.case === 'user' || item.content.case === 'notice' ? item.content.text : item.content.case))
    expect(said.slice(0, 4)).toEqual([
      'persona: coder, the shipped coding prompt, in every request -- -s replaces it',
      'project context: AGENTS.md, 11 words, in every request',
      'attaching . (2 files, 89 bytes)',
      'hi',
    ])
    expect(said.at(-1)).toBe('answer')
    expect(model.transcript.status).toEqual({ case: 'ready' })
  })
})

describe('the header: the model, thinking, the context meter and the inspector', () => {
  const kinds = ModelKind.byBackend(ReadDocument.from(suitesModelsList) as ReadDocument)
  const rows = ModelRow.list(ReadDocument.from(suitesModelsList) as ReadDocument)

  test('the model as the CLI names it, its kind from models list: claude is billed per use', async () => {
    const { model, clock } = await opened(newUnsent)
    await tick(clock)
    expect(model.model).toMatchObject({ name: 'claude', kind: 'unknown', tag: '' })
    model.modelKinds = kinds
    model.modelRows = rows
    expect(model.model).toEqual({ name: 'claude', kind: 'api', tag: 'API · billed per use', where: 'claude answers through the anthropic API, billed per use.' })
    expect(model.menuGroups.map((group) => [group.title, group.items.map((item) => item.name)])).toEqual([
      ['ON THIS MAC', ['helper', 'root', 'scribe', 'vast']],
      ['CLOUD API · BILLED PER USE', ['claude']],
      ['OTHER', ['fasthelper', 'fastroot', 'plain']],
    ])
    expect(model.menuGroups[1]?.items[0]).toEqual({ name: 'claude', sub: 'claude-sonnet-4-5 · no key', kind: 'api', current: true })
  })

  test('picking a model, a mode or an effort writes the set line, between turns; the header keeps the model the CLI says answered', async () => {
    const { model, clock, written } = await opened(newUnsent)
    await tick(clock)
    model.modelRows = rows
    model.toggleMenu('model')
    model.pickModel('root')
    expect(model.menu).toBeUndefined()
    model.pickThink('off')
    model.pickEffort('high')
    model.pickEffort('default')
    expect(written()).toEqual(['{"type":"set","model":"root"}', '{"type":"set","think":"off"}', '{"type":"set","effort":"high"}', '{"type":"set","effort":"auto"}'])
    expect(model.transcript.model).toBe('claude')
    expect(model.menuGroups.flatMap((group) => group.items).find((item) => item.current)?.name).toBe('root')
    expect(model.thinkLabel).toBe('off')
    model.pickThink('on')
    model.pickEffort('max')
    expect(model.thinkLabel).toBe('on · max')
    expect(model.settingsFooter).toBe('Applied from the next turn; apogee says each change.')
  })

  test('the context meter: unknown with no window, not reported with no usage, and its click opens the inspector', async () => {
    const { model, clock } = await opened(newUnsent)
    await tick(clock)
    expect(model.meter.text).toBe('unknown')
    model.contextWindow = 8192
    expect(model.meter.text).toBe('not reported')
    expect(model.inspectorOpen, 'open, as the old inspector: true').toBe(true)
    model.toggleInspector()
    expect(model.inspectorOpen).toBe(false)
    model.showContext()
    expect(model.inspectorOpen).toBe(true)
  })

  test('an execute session’s banner: the CLI’s suite and whether it orchestrates, and the folder it works in', async () => {
    const { model, clock } = await opened(executeProject, { execute: { suite: 'coding' }, folder: '~/code/ingest' })
    await tick(clock)
    expect(model.executeBanner).toBe('execute · suite coding · in ingest')
    expect(model.suiteWords).toBe('suite coding')
  })
})

describe('the composer: the / and @ menus, attach, and the commands', () => {
  test('/ lists the commands it starts; a command with nothing to take runs, one that takes something waits for it', async () => {
    const { model, clock, acts } = await opened(newUnsent)
    await tick(clock)
    model.setDraft('/mo')
    expect(model.slashOpen).toBe(true)
    expect(model.slashItems?.map((item) => item.name)).toEqual(['model', 'models'])
    model.moveSelection(1)
    expect(model.activeIndex).toBe(1)
    model.completeSelection()
    expect(acts).toEqual([{ case: 'openLibrary' }])
    model.setDraft('/temp')
    model.completeSelection()
    expect(model.draft).toBe('/temperature ')
    model.setDraft('/zzz')
    expect(model.slashItems).toEqual([])
    model.dismissMenus()
    expect(model.slashOpen).toBe(false)
  })

  test('a command the window does not act on is said in the toast in the old prototype’s words, never in the transcript', async () => {
    const { model, clock, acts } = await opened(newUnsent)
    await tick(clock)
    model.setDraft('/recall off')
    model.submit()
    model.setDraft('/frobnicate')
    model.submit()
    expect(acts).toEqual([
      { case: 'say', text: '/recall off — noted; this prototype doesn’t act on it' },
      { case: 'say', text: '/frobnicate isn’t a command — type / to see them' },
    ])
    expect(model.items).toEqual([])
  })

  test('@ lists the folder’s files that hold what is typed; picking one attaches it and takes the mention out of the draft', async () => {
    const { model, clock, written } = await opened(newUnsent)
    await tick(clock)
    model.setDraft('look at @run')
    expect(model.mentionOpen).toBe(true)
    expect(model.mentionItems?.map((file) => file.path)).toEqual(['src/ingest/runner.py'])
    model.completeSelection()
    expect(model.draft).toBe('look at ')
    expect(model.attachments).toEqual(['src/ingest/runner.py'])
    expect(written()).toEqual(['{"type":"attach","path":"src/ingest/runner.py"}'])
    model.detach('src/ingest/runner.py')
    expect(model.attachments).toEqual([])
  })

  test('/play in an execute session lists the symphonies after it, and the line is sent as the turn, as typed', async () => {
    const { model, clock, written } = await opened(executeProject, { execute: {} })
    await tick(clock, 5)
    model.plays = [{ name: 'summarize-verify', description: 'Summarize a passage, then check the summary against the passage and correct it.' }]
    model.setDraft('/')
    expect(model.slashItems?.slice(0, 2).map((item) => item.name)).toEqual(['play', 'symphonies'])
    model.setDraft('/play sum')
    expect(model.slashItems).toEqual([{ name: 'play summarize-verify', args: '', summary: 'Summarize a passage, then check the summary against the passage and correct it.', play: true }])
    model.completeSelection()
    expect(model.draft).toBe('/play summarize-verify ')
    model.setDraft('/play summarize-verify The board met.')
    model.submit()
    expect(written()).toEqual(['{"type":"user","text":"/play summarize-verify The board met."}'])
  })

  test('a line made while a turn runs is held, and sent once the turn ends', async () => {
    const { model, clock, written } = await opened(markdownAnswer)
    await tick(clock)
    model.setDraft('go')
    model.submit()
    model.attach('~/code/ingest/')
    expect(model.held).toEqual([{ case: 'attach', path: '~/code/ingest/' }])
    expect(model.settingsFooter).toMatch(/^A turn is running/)
    await tick(clock, 200)
    expect(model.held).toEqual([])
    expect(written().at(-1)).toBe('{"type":"attach","path":"~/code/ingest/"}')
  })
})

describe('the suite chooser and the private confirm', () => {
  const choices = [
    { name: 'fast', footprint: SuiteFootprint.of(ReadDocument.from(statusFast) as ReadDocument), isDefault: true },
    { name: 'vast', footprint: SuiteFootprint.of(ReadDocument.from(statusVast) as ReadDocument), isDefault: false },
  ]

  test('the chooser opens on the suite named; one the CLI prices over this machine switches only forced', async () => {
    const { model, clock, acts } = await opened(newUnsent)
    await tick(clock)
    model.chooseSuite()
    expect(acts).toEqual([{ case: 'chooseSuite' }])
    model.presentSuites(choices, 'fast', 'vast')
    expect(model.suiteWords).toBe('suite fast, the default')
    expect(model.chosenOverBudget).toBe(true)
    expect(model.canSwitch).toBe(false)
    model.toggleForce()
    expect(model.canSwitch).toBe(true)
    model.chooseRow('fast')
    expect(model.suiteChooser).toEqual({ chosen: 'fast', force: false })
    model.dismissSuites()
    expect(model.suiteChooser).toBeUndefined()
  })

  test('a new chat runs under the default suite: the chooser opens on it, its row marked now, and choosing it leaves Switch off', async () => {
    const { model, clock } = await opened(newUnsent)
    await tick(clock)
    model.presentSuites(choices, 'fast')
    expect(model.runningSuite).toBe('fast')
    expect(model.suiteChooser?.chosen).toBe('fast')
    expect(model.suiteSheet().rows.find((row) => row.name === 'fast')?.running).toBe(true)
    expect(model.suiteSheet().rows.filter((row) => row.running).map((row) => row.name)).toEqual(['fast'])
    model.chooseRow('vast')
    model.chooseRow('fast')
    expect(model.canSwitch).toBe(false)
  })

  test('a resumed chat’s suite is the CLI’s to say: no row is marked now until it does', async () => {
    const { model, clock } = await opened(newUnsent, { resume: 'c-1' })
    await tick(clock)
    model.presentSuites(choices, 'fast')
    expect(model.runningSuite).toBeUndefined()
    expect(model.suiteSheet().rows.filter((row) => row.running)).toEqual([])
  })

  test('Switch starts the session again under the suite, as machine mode carries none: --suite and --force on its launch', async () => {
    const { model, clock, child } = await opened(newUnsent)
    await tick(clock)
    model.presentSuites(choices, 'fast', 'vast', true)
    await model.switchSuite()
    expect(child.calls.at(-1)?.arguments.slice(-3)).toEqual(['--suite', 'vast', '--force'])
  })

  test('leaving a private conversation with a turn sent asks first, with the body for a turn running; Keep it running leaves everything as it was', async () => {
    const { model, clock, acts } = await opened(privateUnsent + '{"turn":1,"type":"thinking"}\n', { privately: true })
    await tick(clock, 2)
    model.leave({ type: 'new' })
    expect(acts, 'no turn sent: it leaves at once').toEqual([{ case: 'leave', then: { type: 'new' } }])
    model.setDraft('a secret')
    model.submit()
    await tick(clock)
    model.leave({ type: 'new' })
    expect(acts.at(-1)).toEqual({ case: 'ask', kind: 'endPrivate' })
    expect(model.endPrivate).toEqual({
      title: 'This private conversation can\'t be reopened',
      body: 'Nothing of it is written: switching stops the turn running in it and ends this session\'s apogee, and the conversation goes with it. Keep it running, or end it?',
      confirm: 'End it and switch',
      cancel: 'Keep it running',
    })
    const before = model.items
    model.keepRunning()
    expect(model.endPrivate).toBeUndefined()
    expect(model.items).toBe(before)
    expect(model.connection.case).toBe('connected')
  })

  test('with no turn running the body says so; End it and switch ends the session and carries the leave out', async () => {
    const { model, clock, acts } = await opened(stoppedTurn, { privately: true })
    await tick(clock)
    model.setDraft('Which port?')
    model.submit()
    await tick(clock, 20)
    model.leave({ type: 'private' })
    expect(model.endPrivate?.body).toBe('Nothing of it is written: switching ends this session\'s apogee, and the conversation goes with it. Keep it running, or end it?')
    await model.endAndLeave()
    await settled()
    expect(acts.at(-1)).toEqual({ case: 'leave', then: { type: 'private' } })
    expect(model.transcript.status).toEqual({ case: 'ended' })
  })
})

describe("the face: everything the session's views draw, as words a test can walk", () => {
  test('a new session, ready: the empty state, the header and the composer', async () => {
    const { model, clock } = await opened(newUnsent)
    await tick(clock)
    const face = model.face()
    expect(face.state).toBe('empty')
    expect(face.header).toMatchObject({ title: 'New session', privateBadge: false, banner: '', suiteWords: '', think: 'default', inspectorOpen: true })
    expect(face.header.meter).toEqual({ known: false, text: 'unknown', percent: 0, used: '', window: '', warns: false })
    expect(face.empty).toEqual({ heading: 'What are we working on?', privateWords: '', notices: [], launched: true, privateInstead: true, chooseSuite: true })
    expect(face.composer).toMatchObject({ draft: '', placeholder: 'Message claude', status: '', streaming: false, canSend: false, canAttach: true, attachments: [] })
    expect(face.pending.shown).toBe(false)
    model.knowDefaultSuite('fast')
    expect(model.face().header).toMatchObject({ suiteWords: 'suite fast, the default', canSwitchSuite: true })
    model.knowDefaultSuite('')
    expect(model.face().header.suiteWords, 'models.default_suite unset: a new chat runs under none').toBe('no suite')
  })

  test('not launched (a preset that opens none): the empty state, and nothing offered that needs a session', async () => {
    const child = new StandInChild({ rules: {}, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const ticks = new Clock({})
    const model = new SessionViewModel(new ApogeeClient(child, ticks), ticks, {}, { folder: '', files: [] }, [{ label: 'Folder…', hint: '⇧⌘O', path: '~/code/ingest/' }])
    const acts: SessionAction[] = []
    model.onAction = (act) => acts.push(act)
    const face = model.face()
    expect(face.state).toBe('empty')
    expect(face.empty).toMatchObject({ launched: false, privateInstead: false, chooseSuite: false })
    expect(face.header.canSwitchSuite).toBe(false)
    expect(face.composer.canAttach).toBe(false)
    model.attach('~/code/ingest/')
    model.chooseSuite()
    expect(model.held).toEqual([])
    expect(model.attachments).toEqual([])
    expect(acts).toEqual([])
    expect(child.calls).toEqual([])
  })

  test("a private one: its words and the CLI's notice under the empty state, no Private session instead", async () => {
    const { model, clock } = await opened(privateUnsent, { privately: true })
    await tick(clock, 2)
    expect(model.face().empty).toEqual({
      heading: 'Private session',
      privateWords: 'Nothing of this session is written: no transcript, no recall in later sessions.',
      notices: ['private: nothing of this chat is written -- no transcript, no recall'],
      launched: true,
      privateInstead: false,
      chooseSuite: true,
    })
    expect(model.face().header.privateBadge).toBe(true)
  })

  test('a played turn: its rows, the stages marked, the answer and its footer', async () => {
    const { model, clock } = await opened(executeProject, { execute: {} }, { start: 20, still: true })
    model.writeAhead('hi')
    clock.start()
    await settled()
    const rows = model.face().rows
    expect(rows.map((row) => row.kind)).toEqual(['notice', 'notice', 'notice', 'user', 'notice', 'notice', 'activity', 'answer'])
    expect(rows.at(-1)).toMatchObject({ kind: 'answer', text: 'ROOT<hi>', finished: true, footer: { model: 'root', usage: [] } })
    expect(rows[6]).toMatchObject({ kind: 'activity', summary: 'thought', live: false, lines: [] })
  })

  test('missing: the window says apogee is not installed', async () => {
    const { model } = await opened(newUnsent, {}, {}, 'missing')
    expect(model.face().state).toBe('missing')
  })
})

test("an answer's footer: the model its result names, and the tokens where the provider reported them (no recorded stream reports any, so one result is planted)", async () => {
  const planted = newUnsent + '{"turn":1,"type":"answer_start"}\n{"text":"hi","turn":1,"type":"answer_delta"}\n{"finish_reason":"stop","model":"claude","text":"hi","turn":1,"type":"result","usage":{"input_tokens":3214,"output_tokens":588}}\n'
  const { model, clock } = await opened(planted)
  await tick(clock)
  model.modelKinds = { claude: 'api' }
  model.contextWindow = 32768
  model.setDraft('hello')
  model.submit()
  await tick(clock, 5)
  expect(model.face().rows.at(-1)).toMatchObject({ kind: 'answer', footer: { model: 'claude', kind: 'api', usage: ['in 3,214', 'out 588'] } })
  expect(model.face().header.meter).toEqual({ known: true, text: '10% · 3,214 of 32,768 tokens', percent: 10, used: '3.2K', window: '32.8K', warns: false })
})

test('the facts its owner hands in after a read redraw the window: the window, the model facts, the symphonies, the default suite', async () => {
  const { model, clock } = await opened(newUnsent)
  await tick(clock)
  let redraws = 0
  model.subscribe(() => (redraws += 1))
  model.contextWindow = 8192
  model.modelKinds = { claude: 'api' }
  model.modelRows = []
  model.plays = []
  model.knowDefaultSuite('fast')
  expect(redraws).toBe(5)
})
