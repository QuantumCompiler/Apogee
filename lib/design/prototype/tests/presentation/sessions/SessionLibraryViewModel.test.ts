import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { describe, expect, test } from 'vitest'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { ApogeeClient } from '../../../source/business/session/ApogeeClient'
import { ChatLaunch } from '../../../source/business/session/ChatLaunch'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { scenarioTurns } from '../../../source/presentation/app/ViewModels/AppViewModel'
import { kvChat, retryChat, sessionSamples, streamingRules } from '../../../source/presentation/session/Previews/SessionPresets'
import { ContextMeter } from '../../../source/presentation/session/ViewModels/SessionActions'
import { libraryRules, withLibraryRules } from '../../../source/presentation/sessions/Previews/LibraryRules'
import { SessionLibraryViewModel } from '../../../source/presentation/sessions/ViewModels/SessionLibraryViewModel'
import { chord } from '../../../source/presentation/shell/ViewModels/KeyMap'

const settled = () => new Promise((resolve) => setTimeout(resolve, 0))

/** The Streaming chat's id, as its session line names it. */
const streamingChat = '20261119-111221-123d'

/** A library over the stand-in, answering the Streaming session and the library's reads, still; what it says, asks and opens, kept. */
async function library(options: { rules?: Rules; apogee?: ApogeeMode; open?: boolean } = {}) {
  const streaming = streamingRules('chat')
  const rules = options.rules ?? withLibraryRules({ chats: streaming.chats, reads: streaming.reads, commands: streaming.commands })
  const child = new StandInChild({ rules, apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
  const clock = new Clock({ start: 1, still: true })
  const model = new SessionLibraryViewModel(new ApogeeClient(child, clock), new ReadRunner(child), new JobStarter(child, clock), clock, sessionSamples)
  const said: string[] = []
  const asked: string[] = []
  const presented: string[] = []
  model.say = (text) => said.push(text)
  model.ask = (kind) => asked.push(kind)
  model.present = (kind) => presented.push(kind)
  await model.start()
  if (options.open ?? true) {
    // The Streaming setup, as the composition writes it before the clock starts: the folder attached, its two turns.
    await model.replace({})
    model.current.attach('.')
    for (const text of scenarioTurns.streaming) model.current.writeAhead(text)
  }
  clock.start()
  await settled()
  return { model, child, clock, said, asked, presented }
}

const calls = (child: StandInChild, kind: string) => child.calls.filter((call) => call.kind === kind).map((call) => call.arguments.join(' '))
const titles = (model: SessionLibraryViewModel) => model.listFace().groups.flatMap((group) => group.rows.map((row) => row.title))

describe("the session list (the Mac app's SessionLibraryViewModel, the old prototype's sidebar)", () => {
  test('the recorded chats fall into their date groups: Today and Yesterday open, Previous 7 days and Earlier closed, as the old sidebar opens them', async () => {
    const { model, child } = await library()
    expect(calls(child, 'reads').slice(0, 2)).toEqual(['chats list', 'models list'])
    const face = model.listFace()
    expect(face.count).toBe(13)
    expect(face.groups.map((group) => [group.title, group.count, group.open])).toEqual([
      ['Today', 5, true],
      ['Yesterday', 3, true],
      ['Previous 7 days', 3, false],
      ['Earlier', 2, false],
    ])
    expect(face.groups[0]?.rows.map((row) => [row.title, row.time, row.kind])).toEqual([
      ['TaskGroup timeouts in the ingest runner', '14:03', 'local'],
      ['Make fetch retries configurable', '13:40', 'local'],
      ['Group commit, from the storage notes', '11:52', 'api'],
      ['What’s in this crash screenshot?', '10:18', 'local'],
      ['The board’s minutes, summarized', '09:40', 'local'],
    ])
    expect(face.groups[2]?.rows.find((row) => row.title.startsWith('Postgres'))?.kind, 'a subscription').toBe('subscription')
  })

  test('the open session is its row, current, and the header takes the list’s name', async () => {
    const { model } = await library()
    expect(model.current.transcript.chatID).toBe(streamingChat)
    expect(model.listFace().groups[0]?.rows[0]).toMatchObject({ id: streamingChat, current: true })
    expect(model.current.face().header.title).toBe('TaskGroup timeouts in the ingest runner')
  })

  test('off Sessions no row is current, as the old `s.screen === \'chat\'` test has it: no row, no closed group’s mark, no private row; back on Sessions, all three again', async () => {
    const { model } = await library()
    model.toggleGroup('Today')
    const currents = () => model.listFace().groups.flatMap((group) => group.rows.filter((row) => row.current).map((row) => row.id))
    expect(currents()).toEqual([streamingChat])
    expect(model.listFace().groups[0]?.hasCurrent).toBe(true)
    model.showing(false)
    expect(currents()).toEqual([])
    expect(model.listFace().groups.some((group) => group.hasCurrent)).toBe(false)
    model.showing(true)
    expect(currents()).toEqual([streamingChat])
    expect(model.listFace().groups[0]?.hasCurrent).toBe(true)
    await model.replace({ privately: true })
    await settled()
    expect(model.listFace().privateRow).toEqual({ shown: true, current: true })
    model.showing(false)
    expect(model.listFace().privateRow).toEqual({ shown: true, current: false })
  })

  test('a closed group holding the open session says so; a group opens and closes; every date group closes and opens at once', async () => {
    const { model } = await library()
    model.toggleGroup('Today')
    expect(model.listFace().groups[0]).toMatchObject({ open: false, hasCurrent: true })
    expect(model.listFace().anyDateOpen).toBe(true)
    model.toggleDates()
    expect(model.listFace().groups.map((group) => group.open)).toEqual([false, false, false, false])
    expect(model.listFace().datesLabel).toBe('Expand every date group')
    model.toggleDates()
    expect(model.listFace().groups.map((group) => group.open)).toEqual([true, true, true, true])
  })

  test('search filters by name, any case, every group open while it searches; one with no match says "No sessions match …"', async () => {
    const { model } = await library()
    model.setQuery('TIMEOUTS')
    expect(titles(model)).toEqual(['TaskGroup timeouts in the ingest runner', 'asyncio timeouts in the scraper'])
    expect(model.listFace().groups.every((group) => group.open)).toBe(true)
    expect(model.listFace().matchText).toBe('2 matching · groups open while you search')
    model.setQuery('kubernetes')
    expect(model.listFace().groups).toEqual([])
    expect(model.listFace().noResults).toBe('No sessions match “kubernetes”.')
    expect(model.listFace().count, 'the count is every chat').toBe(13)
  })

  test("each row's menu: Details…, Rename…, Compact… and Delete…, the last in the error color", async () => {
    const { model } = await library()
    model.toggleRowMenu(kvChat)
    const row = model.listFace().groups[1]?.rows.find((entry) => entry.id === kvChat)
    expect(row?.menuOpen).toBe(true)
    expect(row?.items.map((item) => [item.label, item.destructive])).toEqual([
      ['Details…', false],
      ['Rename…', false],
      ['Compact…', false],
      ['Delete…', true],
    ])
    model.closeRowMenu()
    expect(model.listFace().groups[1]?.rows.some((entry) => entry.menuOpen)).toBe(false)
  })

  test('with no binary the list says so in the Mac app’s words; a list the CLI refuses says why in its words', async () => {
    const missing = await library({ apogee: 'missing', open: false })
    expect(missing.model.listFace()).toMatchObject({ groups: [], empty: 'Apogee isn’t installed, so there are no sessions to list.' })
    const typo = await library({ apogee: 'config-typo', open: false })
    expect(typo.model.listFace().empty).toMatch(/^The sessions could not be read: apogee chats: /)
  })
})

describe("the row menu's acts", () => {
  test('Details… reads chats info and shows its fields, under the text’s labels', async () => {
    const { model, child, presented } = await library()
    model.rowAct(kvChat, 'details')
    await settled()
    expect(calls(child, 'reads')).toContain(`chats info ${kvChat}`)
    expect(presented).toEqual(['chatInfo'])
    expect(model.info?.title).toBe('Where llama.cpp sizes the KV cache')
    expect(model.info?.fields.map((field) => field.label)).toEqual(['id', 'title', 'backend', 'started', 'updated', 'turns', 'messages', 'compacted'])
    expect(model.info?.command).toBe(`apogee chats info ${kvChat}`)
  })

  test('Rename runs chats title, the name after --, and says so; an empty name is no rename', async () => {
    const { model, child, presented, said } = await library()
    model.rowAct(kvChat, 'rename')
    expect(presented).toEqual(['rename'])
    expect(model.renaming).toMatchObject({ name: 'Where llama.cpp sizes the KV cache', canRename: true })
    model.setRenameName('   ')
    expect(model.renaming?.canRename).toBe(false)
    model.setRenameName('KV cache, sized')
    await model.rename()
    expect(calls(child, 'commands')).toContain(`chats title ${kvChat} -- KV cache, sized`)
    expect(said.at(-1)).toBe('Renamed to KV cache, sized')
    expect(model.renaming).toBeUndefined()
  })

  test('Delete runs chats delete only after the confirm', async () => {
    const { model, child, asked, said } = await library()
    model.rowAct(kvChat, 'delete')
    expect(asked).toEqual(['deleteChat'])
    expect(model.confirmWords('deleteChat')).toEqual({
      title: 'Delete “Where llama.cpp sizes the KV cache”?',
      body: `apogee chats delete ${kvChat} -- the conversation and its turns are removed, and this can't be undone.`,
      confirm: 'Delete',
      cancel: 'Cancel',
    })
    model.dismissPending()
    expect(calls(child, 'commands').filter((call) => call.startsWith('chats delete'))).toEqual([])
    model.rowAct(kvChat, 'delete')
    await model.confirm()
    expect(calls(child, 'commands')).toContain(`chats delete ${kvChat}`)
    expect(said.at(-1)).toBe('Deleted Where llama.cpp sizes the KV cache')
  })

  test('Compact… asks first; a saved chat then compacts as a job, its row busy, Stop compacting in its menu; Stop says "Not compacted: stopped"', async () => {
    const streaming = streamingRules('chat')
    const rules = withLibraryRules({ ...streaming, jobs: [{ arguments: ['chats', 'compact', kvChat], answer: { stdout: 'compacted', exitAfter: 30 } }] })
    const { model, child, asked, said } = await library({ rules })
    model.rowAct(kvChat, 'compact')
    expect(asked).toEqual(['compactChat'])
    expect(model.confirmWords('compactChat')?.body).toBe(
      `apogee chats compact ${kvChat} --yes -- its history is folded into a summary the model carries on from. The turns before it aren't kept word for word, and this can't be undone.`,
    )
    void model.confirm()
    await settled()
    expect(calls(child, 'jobs')).toEqual([`chats compact ${kvChat} --yes`])
    const row = () => model.listFace().groups[1]?.rows.find((entry) => entry.id === kvChat)
    expect(row()).toMatchObject({ busy: true, help: `Where llama.cpp sizes the KV cache · compacting: apogee chats compact ${kvChat} --yes` })
    expect(row()?.items.map((item) => item.label)).toEqual(['Details…', 'Rename…', 'Stop compacting', 'Delete…'])
    model.rowAct(kvChat, 'stopCompacting')
    await settled()
    await settled()
    expect(said.at(-1)).toBe('Not compacted: stopped')
    expect(row()?.busy).toBe(false)
  })

  test('the open session’s own chat compacts in its session, as /compact does', async () => {
    const { model, said } = await library()
    model.rowAct(streamingChat, 'compact')
    expect(model.confirmWords('compactChat')?.body).toBe(
      "Its session compacts it, as /compact does: the history so far is folded into a summary the model carries on from. The turns before it aren't kept word for word.",
    )
    await model.confirm()
    expect(said.at(-1)).toBe('Compacting TaskGroup timeouts in the ingest runner in its session')
    // Held for the turn running, as /compact is.
    expect(model.current.held.map((line) => line.case)).toEqual(['compact'])
  })
})

describe('the inspector', () => {
  test('six sections, the old ones open (session, turn, context, model); expand all opens every one, then collapse all closes them', async () => {
    const { model } = await library()
    expect(model.inspector().sections).toEqual({ session: true, turn: true, context: true, model: true, helpers: false, recall: false })
    expect(model.inspector().allLabel).toBe('Expand all')
    model.toggleAllSections()
    expect(Object.values(model.inspector().sections)).toEqual([true, true, true, true, true, true])
    expect(model.inspector().allLabel).toBe('Collapse all')
    model.toggleAllSections()
    expect(Object.values(model.inspector().sections)).toEqual([false, false, false, false, false, false])
    model.toggleSection('recall')
    expect(model.inspector().sections.recall).toBe(true)
  })

  test("its session: the list's row -- name, turns, when, id -- with Details… and Compact…; its turn from the replayed events; its model from models info; its helpers from models status; recall from chats recall", async () => {
    const { model, child } = await library()
    await model.inspectorAppeared()
    const face = model.inspector()
    expect(face.turnLabel).toBe('· turn at 14:03')
    expect(face.session.lines).toEqual([
      { label: 'Name', value: 'TaskGroup timeouts in the ingest runner', mono: false },
      { label: 'Turns', value: '2', mono: false },
      { label: 'Updated', value: '2026-11-14T14:03:07Z', mono: true },
      { label: 'Id', value: streamingChat, mono: true },
    ])
    expect(face.session.chatActs).toBe(true)
    expect(face.turn.status).toEqual({ words: 'streaming', tone: 'accent' })
    expect(face.turn.rows.map((row) => [row.label, row.value])).toEqual([
      ['Thinking', 'default'],
      ['Utility', 'rewriting the follow-up into a search query with root'],
      ['Memory', 'Recalled 1 past session'],
      ['Search', 'src/ingest/runner.py'],
      ['fetch_url', 'docs.python.org'],
      ['read_file', 'src/ingest/runner.py'],
      ['Usage', 'reported when the turn ends'],
    ])
    expect(face.model.summary).toMatchObject({ name: 'root', kind: 'local' })
    expect(face.model.facts.map((fact) => fact.label)).toEqual(['Format', 'Runs on', 'Size', 'Window'])
    expect(face.context).toMatchObject({ summary: 'unknown', window: '8,192-token window' })
    // The CLI's session line says it runs under no suite: the roles read under `off` (hand-made: models-status-off-handmade.json).
    expect(face.helpers.summary).toBe('2 set · 3 fall back')
    expect(face.helpers.rows.map((row) => row.label)).toEqual(['Embedding', 'Vision', 'Transcription', 'Utility', 'Extraction'])
    expect(face.recall.line).toBe('recall holds 8 chat summaries in /Users/someone/.apogee/memory/chats.db')
    expect(calls(child, 'reads')).toEqual(expect.arrayContaining(['models status --suite off', 'chats recall']))
  })

  test('its reads are made once a session, however often it appears (the Mac’s .task on each appearance reads again; here a redraw would)', async () => {
    const { model, child } = await library()
    await model.inspectorAppeared()
    await model.inspectorAppeared()
    model.toggleInspector()
    model.toggleInspector()
    await settled()
    expect(calls(child, 'reads').filter((call) => call === 'chats recall')).toHaveLength(1)
    expect(calls(child, 'reads').filter((call) => call.startsWith('models status'))).toHaveLength(1)
    await model.replace({ privately: true })
    await settled()
    expect(calls(child, 'reads').filter((call) => call === 'chats recall'), 'a new session reads them again').toHaveLength(2)
  })

  test('Forget every summary… asks first, naming what recall holds, then runs chats recall --forget --yes and says its line', async () => {
    const { model, child, asked, said } = await library()
    await model.askToForgetRecall()
    expect(asked).toEqual(['forget'])
    expect(model.confirmWords('forget')).toMatchObject({
      title: 'Forget what recall holds?',
      body: "Now: recall holds 8 chat summaries in /Users/someone/.apogee/memory/chats.db. apogee chats recall --forget --yes -- every chat's summary goes, so no later session recalls them; every transcript stays.",
      confirm: 'Forget every summary',
    })
    await model.confirm()
    expect(calls(child, 'commands')).toContain('chats recall --forget --yes')
    expect(said.at(-1)).toBe('forgot 8 chat summaries -- every transcript stays; chats finished from now on are summarised again (apogee config set memory.recall false stops it)')
  })

  test('"Summarize older turns now" waits for a known context of 4,000 tokens or more, as the old !ctxKnown term does: an unknown window keeps it off', async () => {
    const { model } = await library()
    const meter = (used: number | undefined, window: number | undefined) => Object.defineProperty(model.current, 'meter', { configurable: true, get: () => ContextMeter.of(used, window) })
    meter(5000, undefined)
    expect(model.inspector().context.canCompact, 'used reported, no window: unknown').toBe(false)
    meter(5000, 8192)
    expect(model.inspector().context.canCompact).toBe(true)
    meter(3999, 8192)
    expect(model.inspector().context.canCompact).toBe(false)
  })

  test('THIS TURN starts afresh when a fresh child numbers its turns from 1 again: a private session started again shows its new child’s turn, not the ended one’s', async () => {
    const opening = '{"model":"claude","next_turn":1,"private":true,"protocol_version":1,"type":"session"}'
    const turn = (helper: string, number: number) => [
      `{"text":"utility — ${helper}","turn":${number},"type":"tool_status"}`,
      `{"finish_reason":"stop","model":"claude","text":"Said.","turn":${number},"type":"result"}`,
    ]
    const first = [opening, ...turn('the first child’s turn 1', 1), ...turn('the first child’s turn 2', 2)].join('\n')
    const second = [opening, ...turn('the second child’s turn 1', 1)].join('\n')
    const rules = withLibraryRules({ chats: [{ arguments: ChatLaunch.arguments({ privately: true }), answer: [{ stdout: first, exit: 2 }, { stdout: second }] }] })
    const { model, clock } = await library({ rules, open: false })
    await model.replace({ privately: true })
    model.current.writeAhead('one')
    model.current.writeAhead('two')
    clock.advance(10)
    await settled()
    expect(model.current.transcript.status.case, 'the first child exited 2').toBe('ended')
    expect(model.inspector().turn.rows.map((row) => row.value)).toContain('the first child’s turn 2')
    await model.current.start()
    model.current.writeAhead('three')
    clock.advance(10)
    await settled()
    const values = model.inspector().turn.rows.map((row) => row.value)
    expect(values).toContain('the second child’s turn 1')
    expect(values).not.toContain('the first child’s turn 2')
  })

  test('⌥⌘I shows and hides the inspector, and a new session keeps it as it was', async () => {
    const { model } = await library()
    expect(model.press(chord('i', 'option', 'command'))).toBe(true)
    expect(model.current.inspectorOpen).toBe(false)
    await model.replace({ privately: true })
    expect(model.current.inspectorOpen).toBe(false)
    model.press(chord('i', 'option', 'command'))
    expect(model.current.inspectorOpen).toBe(true)
    expect(model.press(chord('x', 'command'))).toBe(false)
  })
})

describe('the execute door', () => {
  const doorRules = () => {
    const rules = libraryRules()
    return {
      ...rules,
      commands: [
        ...rules.commands,
        { arguments: ['config', 'get', 'suites'], answer: { stdout: 'cloud\ncoding\nfast\n' } },
        { arguments: ['config', 'get', 'models.default_suite'], answer: { stdout: 'coding\n' } },
      ],
      reads: [...rules.reads, { arguments: ['models', 'list'], answer: { stdout: '{"data":[],"object":"list"}' } }],
    }
  }

  test("⌥⌘N opens it with the suites read, and its line reads cd ~/code/ingest && apogee execute --tools --suite coding --orchestrate", async () => {
    const { model, child, presented } = await library({ rules: doorRules(), open: false })
    model.press(chord('n', 'option', 'command'))
    await settled()
    expect(presented).toEqual(['execute'])
    expect(calls(child, 'commands')).toEqual(['config get suites', 'config get models.default_suite'])
    const before = model.door()
    expect(before.suites.map((option) => option.label)).toEqual(['The default suite (coding)', 'cloud', 'coding', 'fast'])
    model.setExecuteForm({ suite: 'coding', folder: '~/code/ingest', orchestrate: true })
    expect(model.door().command).toBe('cd ~/code/ingest && apogee execute --tools --suite coding --orchestrate')
    expect(model.door().models[0]?.label).toBe('The suite’s chat member')
  })

  test('Orchestrate is disabled when no suite is named and none is the default', async () => {
    const rules = doorRules()
    const bare = { ...rules, commands: rules.commands.map((rule) => (rule.arguments.at(-1) === 'models.default_suite' ? { ...rule, answer: { stdout: '\n' } } : rule)) }
    const { model } = await library({ rules: bare, open: false })
    await model.openExecuteDoor()
    expect(model.canOrchestrate).toBe(false)
    expect(model.door()).toMatchObject({ canOrchestrate: false, suites: [{ label: 'No suite: bare on one model' }, { label: 'cloud' }, { label: 'coding' }, { label: 'fast' }] })
    model.setExecuteForm({ suite: 'cloud' })
    expect(model.canOrchestrate).toBe(true)
  })
})

test('the compact jobs are the library’s: retry’s and kv’s side by side (GUI 6b)', async () => {
  const streaming = streamingRules('start')
  const rules = withLibraryRules({ ...streaming, jobs: [retryChat, kvChat].map((id) => ({ arguments: ['chats', 'compact', id, '--yes'], answer: { stdout: 'compacted', exitAfter: 30 } })) })
  const { model } = await library({ rules })
  void model.compactions.compact(retryChat)
  void model.compactions.compact(kvChat)
  await settled()
  expect(model.listFace().groups.flatMap((group) => group.rows).filter((row) => row.busy).map((row) => row.id)).toEqual([retryChat, kvChat])
})

describe('a leave that lands with a file and a draft (design 1o: the Code graph’s Ask about this)', () => {
  test('a new session opens on Sessions with the file attached as a chip and the draft in the composer; nothing sent', async () => {
    const { model, child } = await library()
    let shown = 0
    model.showSessions = () => (shown += 1)
    const before = model.current
    await model.carryOut({ type: 'new', attach: '/Users/someone/src/ingest/runner.py', draft: 'Explain Runner.run_batch and what depends on it.' })
    await settled()
    expect(model.current).not.toBe(before)
    const composer = model.current.face().composer
    expect([shown, composer.draft, composer.attachments]).toEqual([1, 'Explain Runner.run_batch and what depends on it.', ['/Users/someone/src/ingest/runner.py']])
    expect(model.current.face().rows.filter((row) => row.kind === 'user')).toEqual([])
    expect(child.unanswered).toEqual([])
  })
})
