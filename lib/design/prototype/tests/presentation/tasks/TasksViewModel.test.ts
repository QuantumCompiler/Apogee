import runRefused from '@recorded/presentation/tasks/Fixtures/task-run-refused.err?raw'
import resumeRefused from '@recorded/presentation/tasks/Fixtures/task-resume-refused.err?raw'
import { describe, expect, test } from 'vitest'
import { newTaskGoal, recordedRun, reportRun, tasks as ids } from '../../../source/presentation/tasks/Previews/TasksWorlds'
import { callsSince, settle, tasksPlace } from './TasksHarness'

const row = (place: ReturnType<typeof tasksPlace>) => place.tasks.face().list.map((each) => [each.goal, each.status, each.tone])

describe('the Tasks place over the recorded ledger (design 1p): the Mac’s TasksViewModel, drawn as the old prototype’s place', () => {
  test('the ledger: each row in the old words and colors -- a killed task still reading running, as the old row does -- the first one shown, read', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    expect(row(place)).toEqual([
      ['Answer when asked', 'done · every check passed', 'plain'],
      ['say hello', 'exhausted · 1 of 1 rounds used', 'warning'],
      ['say hello', 'running · round 1 of 2', 'accent'],
    ])
    expect(callsSince(place.child)).toEqual(['reads task list', `reads task status ${ids.done}`])
    expect(place.tasks.face().list.map((each) => each.current)).toEqual([true, false, false])
    expect(place.tasks.face().truncation).toBe('')
  })

  test('Show all appears only past 50 rows, and lists all 60 (task list --all) for the life of the place', async () => {
    const place = tasksPlace({ world: 'sweep' })
    await place.tasks.appeared()
    expect([place.tasks.face().list.length, place.tasks.face().truncation]).toEqual([50, 'The newest 50 of 60 tasks'])
    await place.tasks.showAll()
    expect([place.tasks.face().list.length, place.tasks.face().truncation]).toEqual([60, ''])
    await place.tasks.appeared()
    expect(callsSince(place.child).filter((call) => call.startsWith('reads task list'))).toEqual(['reads task list', 'reads task list --all', 'reads task list --all'])
    const small = tasksPlace()
    await small.tasks.appeared()
    expect(small.tasks.face().truncation).toBe('')
  })

  test('a live task plays through its rounds from the recorded stream, its row first and shown, the shared job list holding it while it runs', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(recordedRun)
    await place.tick(16)
    let face = place.tasks.face()
    expect(face.list[0]).toMatchObject({ id: ids.live, goal: 'say hello', status: 'running · round 1 of 2', tone: 'accent', current: true })
    expect(face.detail).toMatchObject({ id: ids.live, status: 'running · round 1 of 2', isRunning: true, canPause: true, canStop: true, canResume: false, canRerun: false })
    expect(face.detail.rounds.map((each) => [each.n, each.kind, each.text, each.outcome])).toEqual([
      ['Plan', 'plan', 'the plan turn wrote 1 step', 'recorded'],
      ['Round 1', 'execute', 'working…', 'in flight'],
    ])
    expect(face.detail.plan).toEqual([{ n: '1', text: 'mock response' }])
    expect(place.registry.children('job').map((child) => child.label)).toEqual(['apogee task run say hello'])
    await place.tick(20)
    face = place.tasks.face()
    expect(face.detail).toMatchObject({ status: 'exhausted · 2 of 2 rounds used', isRunning: false, canPause: false, canStop: false, canResume: false, canRerun: true, rerunLabel: 'Run again with 4 rounds' })
    expect(face.detail.rounds.map((each) => [each.n, each.text, each.outcome])).toEqual([
      ['Plan', 'the plan turn wrote 1 step', 'recorded'],
      ['Round 1', 'no status reported; tools: 0 ran', 'not done'],
      ['Round 2', 'no status reported; tools: 0 ran', 'not done'],
    ])
    expect(face.detail.checks).toEqual([{ cmd: 'require · the answer contains "mock"', result: 'passed · found in the answer', tone: 'accent' }])
    expect(face.list.map((each) => each.goal)).toEqual(['say hello', 'Answer when asked', 'say hello', 'say hello'])
    expect(place.registry.children('job')).toEqual([])
  })

  test('Stop after this round: task halt, then the run stops when the round in flight ends -- said meanwhile, as the old one says it', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(recordedRun)
    await place.tick(14)
    const from = place.child.calls.length
    await place.tasks.halt()
    expect(callsSince(place.child, from)).toContain(`commands task halt ${ids.live}`)
    expect(place.tasks.face().detail).toMatchObject({ status: 'running · halts when round 1 ends', canPause: false, canStop: true })
    await place.tick(10)
    const detail = place.tasks.face().detail
    expect(detail).toMatchObject({ status: 'halted · resumes from its ledger', tone: 'warning', isRunning: false, canStop: false, canResume: true, showWhere: true, resumeWhere: 'in /Users/someone/work, where it was started' })
    expect(detail.rounds.map((each) => [each.n, each.outcome])).toEqual([
      ['Plan', 'recorded'],
      ['Round 1', 'not done'],
    ])
    expect(place.said).toEqual([])
  })

  test('Stop now: task cancel, and the turn in flight ends now, cancelled', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(recordedRun)
    await place.tick(14)
    await place.tasks.cancel()
    await place.tick(4)
    const detail = place.tasks.face().detail
    expect(detail).toMatchObject({ status: 'cancelled by you', tone: 'plain', canResume: true, canStop: false })
    expect(detail.rounds.map((each) => [each.n, each.outcome])).toEqual([
      ['Plan', 'recorded'],
      ['Round 1', 'cancelled'],
    ])
    expect(place.tasks.face().list[0]?.status).toBe('cancelled by you')
  })

  // task-run.jsonl: the plan recorded at its 11th line, round 1 from the 12th to its end at the 20th, round 2 to the 29th.
  const rows = (place: ReturnType<typeof tasksPlace>) => place.tasks.face().detail.rounds.map((each) => [each.n, each.outcome])

  test('a halt asked between turns lands as the next turn ends, as the CLI reads one only as a turn ends (runner.cpp’s stop_if_asked) and as the old haltAt does: after the plan, round 1’s end', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(recordedRun)
    await place.tick(11)
    await place.tasks.halt()
    expect(place.tasks.face().detail.status).toBe('running · halts when round 1 ends')
    expect(place.tasks.face().list[0]?.status).toBe('running · halts when round 1 ends')
    await place.tick(5)
    expect(place.tasks.face().detail.status, 'round 1 runs').toBe('running · halts when round 1 ends')
    await place.tick(10)
    expect(place.tasks.face().detail).toMatchObject({ status: 'halted · resumes from its ledger', canResume: true })
    expect(rows(place)).toEqual([
      ['Plan', 'recorded'],
      ['Round 1', 'not done'],
    ])
  })

  test('a halt asked after round 1 lands as round 2 ends, and round 2, the last of the budget, decides first: exhausted, as recorded', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(recordedRun)
    await place.tick(20)
    await place.tasks.halt()
    expect(place.tasks.face().detail.status).toBe('running · halts when round 2 ends')
    await place.tick(12)
    expect(place.tasks.face().detail.status).toBe('exhausted · 2 of 2 rounds used')
    expect(rows(place)).toEqual([
      ['Plan', 'recorded'],
      ['Round 1', 'not done'],
      ['Round 2', 'not done'],
    ])
  })

  test('where a halt lands is fixed as it is asked: asked in round 1, the page names round 1 until the run stops, never round 2 once round 1 lands', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(recordedRun)
    await place.tick(14)
    await place.tasks.halt()
    await place.tick(6)
    expect(rows(place).at(-1), 'round 1 has landed').toEqual(['Round 1', 'not done'])
    expect(place.tasks.face().detail.status).toBe('running · halts when round 1 ends')
    expect(place.tasks.face().list[0]?.status).toBe('running · halts when round 1 ends')
    await place.tick(5)
    expect(place.tasks.face().detail.status).toBe('halted · resumes from its ledger')
  })

  test('asked in the plan turn, a halt lands as the plan ends', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(recordedRun)
    await place.tick(6)
    await place.tasks.halt()
    expect(place.tasks.face().detail.status).toBe('running · halts when the plan ends')
    await place.tick(10)
    expect(place.tasks.face().detail.status).toBe('halted · resumes from its ledger')
    expect(rows(place)).toEqual([['Plan', 'recorded']])
  })

  test('Resume: Resuming… and the stops while it starts up; a halt sent then is kept, and the run ends halted before its next round', async () => {
    const place = tasksPlace({ world: 'resuming' })
    await place.tasks.appeared()
    await place.tasks.select(ids.stalled)
    expect(place.tasks.face().detail).toMatchObject({ status: 'stalled · resumes from its ledger', canResume: true, canStop: false })
    place.tasks.resume()
    await place.tick(1)
    expect(place.tasks.face().detail).toMatchObject({ isResuming: true, canResume: false, canPause: true, canStop: true, showWhere: true, status: 'stalled · resumes from its ledger' })
    expect(callsSince(place.child).at(-1)).toBe(`jobs task resume ${ids.stalled} --output-format stream-json`)
    await place.tasks.halt()
    await place.tick(10)
    const detail = place.tasks.face().detail
    expect(detail).toMatchObject({ status: 'halted · resumes from its ledger', isResuming: false, canResume: true })
    expect(detail.rounds.map((each) => each.n)).toEqual(['Plan', 'Round 1', 'Round 2'])
  })

  // task-resume.jsonl: its session line, held three ticks as it starts up, then its task_started (the 2nd line) on the 5th tick, round 3 from the 3rd.
  test('a halt sent while the resume starts up is said as the CLI ends it once its task_started arrives: before round 3, with nothing run', async () => {
    const place = tasksPlace({ world: 'resuming' })
    await place.tasks.appeared()
    await place.tasks.select(ids.stalled)
    place.tasks.resume()
    await place.tick(1)
    await place.tasks.halt()
    await place.tick(4)
    expect(place.tasks.face().detail).toMatchObject({ isResuming: false, status: 'running · halts before round 3', canPause: false })
    await place.tick(5)
    expect(place.tasks.face().detail.status).toBe('halted · resumes from its ledger')
    expect(place.tasks.face().detail.rounds.map((each) => each.n)).toEqual(['Plan', 'Round 1', 'Round 2'])
  })

  test('once the resume has begun (its task_started read), a stop is no longer one sent while it starts up: a halt lets round 3 end, done; a cancel ends round 3 now', async () => {
    const halted = tasksPlace({ world: 'resuming' })
    await halted.tasks.appeared()
    await halted.tasks.select(ids.stalled)
    halted.tasks.resume()
    await halted.tick(5)
    expect(halted.tasks.face().detail).toMatchObject({ isResuming: false, isRunning: true })
    await halted.tasks.halt()
    expect(halted.tasks.face().detail.status).toBe('running · halts when round 3 ends')
    await halted.tick(30)
    expect(halted.tasks.face().detail.status).toBe('done · every check passed')

    const cancelled = tasksPlace({ world: 'resuming' })
    await cancelled.tasks.appeared()
    await cancelled.tasks.select(ids.stalled)
    cancelled.tasks.resume()
    await cancelled.tick(5)
    await cancelled.tasks.cancel()
    await cancelled.tick(5)
    expect(cancelled.tasks.face().detail.status).toBe('cancelled by you')
    expect(cancelled.tasks.face().detail.rounds.map((each) => [each.n, each.outcome])).toEqual([
      ['Plan', 'recorded'],
      ['Round 1', 'not done'],
      ['Round 2', 'not done'],
      ['Round 3', 'cancelled'],
    ])
  })

  test('Resume runs on to done: its plan and every earlier round kept, the row saying so over the ledger’s stalled', async () => {
    const place = tasksPlace({ world: 'resuming' })
    await place.tasks.appeared()
    await place.tasks.select(ids.stalled)
    place.tasks.resume()
    await place.tick(30)
    const face = place.tasks.face()
    expect(face.detail).toMatchObject({ status: 'done · every check passed', canResume: false, canRerun: false })
    expect(face.detail.rounds.map((each) => [each.n, each.outcome])).toEqual([
      ['Plan', 'recorded'],
      ['Round 1', 'not done'],
      ['Round 2', 'not done'],
      ['Round 3', 'done'],
    ])
    expect(face.list.find((each) => each.id === ids.stalled)?.status).toBe('done · every check passed')
  })

  test('an interrupted task shows Resume, where, the old line and no stops; an exhausted one no Resume, and Run again with twice its rounds', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    await place.tasks.select(ids.interrupted)
    expect(place.tasks.face().detail).toMatchObject({
      status: 'running, interrupted',
      tone: 'warning',
      canResume: true,
      canPause: false,
      canStop: false,
      interrupted: true,
      interruptedLine: 'Its process is gone: it was killed mid-round, and Resume runs that round again.',
      showWhere: true,
    })
    expect(place.tasks.face().detail.rounds.map((each) => [each.n, each.outcome, each.tone])).toEqual([
      ['Plan', 'recorded', 'plain'],
      ['Round 1', 'in flight', 'warning'],
    ])
    await place.tasks.select(ids.exhausted)
    expect(place.tasks.face().detail).toMatchObject({ canResume: false, showWhere: false, canRerun: true, rerunLabel: 'Run again with 2 rounds' })
  })

  test('Run again (prototype-only): twice the rounds, at most 32, the same goal, checks and grants -- the recorded run of its goal replayed', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    await place.tasks.select(ids.exhausted)
    place.tasks.rerun()
    await place.tick(2)
    expect(callsSince(place.child).filter((call) => call.startsWith('jobs'))).toEqual(['jobs task run say hello --require hello --rounds 2 --output-format stream-json'])
    expect(place.tasks.face().detail).toMatchObject({ id: ids.live, isRunning: true })
  })

  test('a refused start leaves no row: the sheet closes, the CLI’s line word for word in the alert and the toast', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    await place.tasks.openNew()
    expect(place.presented).toEqual(['newtask'])
    place.tasks.setForm({ goal: newTaskGoal, writes: 'allow', cmds: 'allow' })
    place.tasks.start()
    expect(place.dismissed()).toBe(1)
    await place.tick(2)
    expect(place.tasks.face().list.length).toBe(3)
    expect(place.tasks.failure).toBe(runRefused.trim())
    expect(place.said).toEqual([`That didn't work: ${runRefused.trim()}`])
    expect(place.asked).toEqual(['taskRefused'])
    expect(place.tasks.confirmWords('taskRefused')).toEqual({ title: "That didn't work", body: runRefused.trim(), confirm: 'OK' })
    place.tasks.dismissFailure()
    expect(place.tasks.confirmWords('taskRefused')).toBeUndefined()
  })

  test('a refused resume keeps its line on the task’s page, word for word, and Resume is offered again', async () => {
    const place = tasksPlace({ world: 'resumerefused' })
    await place.tasks.appeared()
    await place.tasks.select(ids.stalledGranted)
    place.tasks.resume()
    await place.tick(2)
    const detail = place.tasks.face().detail
    expect(detail.refused).toBe(resumeRefused.trim())
    expect(detail.canResume).toBe(true)
    expect(place.tasks.failure).toBe(resumeRefused.trim())
    expect(place.asked).toEqual(['taskRefused'])
  })

  test('a use the grants cover lands under Allowed, one they don’t under Denied, each with its round; a round’s refusals on its row; the run’s calls its activity', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(reportRun)
    await place.tick(40)
    const detail = place.tasks.face().detail
    expect(detail.id).toBe(ids.report)
    expect(detail.allowed).toEqual(["round 1: run_command on ls -- this task's grant"])
    expect(detail.denied).toEqual(['round 1: write_file on report.txt -- nobody present to allow it'])
    expect(detail.rounds[1]).toMatchObject({ n: 'Round 1', text: 'reported done; tools: 1 ran, 1 denied', outcome: 'not done', denied: 'denied: write_file on report.txt -- nobody present to allow it' })
    expect(detail.activity).toEqual([
      { tool: 'write_file', target: 'report.txt', result: 'failed', tone: 'plain' },
      { tool: 'run_command', target: 'ls', result: 'done', tone: 'quiet' },
    ])
    expect(detail.meta).toBe('in /Users/someone/work · started Nov 9 02:12 · run_command granted · task-20261109-021211')
    expect(detail.budget).toEqual([
      { label: 'Rounds', value: '1 of 1', pct: 100, hasBar: true, tone: 'warning' },
      { label: 'Time so far', value: '0:00', pct: 0, hasBar: false, tone: 'plain' },
      { label: 'Tokens · local, no charge', value: '0', pct: 0, hasBar: false, tone: 'plain' },
    ])
    expect(detail.grants).toEqual({ shown: true, text: 'run_command' })
    expect(detail.checks).toEqual([{ cmd: 'require-file · the file /Users/someone/work/report.txt exists and is not empty', result: 'no such file', tone: 'warning' }])
    expect(detail.perms).toBe('A task never stops to ask. run_command was granted when it started; anything else is refused, the task is told, and every use is recorded in its ledger.')
  })

  test('while a round runs, a call its grant let through shows under Allowed before the round lands', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    place.tasks.startRun(reportRun)
    // task-run-uses.jsonl: its task_grant is the 32nd line, its round's end the 33rd.
    await place.tick(32)
    expect(place.tasks.face().detail.allowed).toEqual(["round 1: run_command on ls -- this task's grant"])
    expect(place.tasks.face().detail.denied).toEqual([])
  })

  test('the sheet: the recipes and the suites read as it opens, the default suite chosen; Start off with no goal, or a recipe that takes input and has none', async () => {
    const place = tasksPlace()
    await place.tasks.appeared()
    const from = place.child.calls.length
    await place.tasks.openNew()
    expect(callsSince(place.child, from)).toEqual(['reads task recipes', 'commands config get suites', 'reads models status'])
    let sheet = place.tasks.sheetFace()
    expect(sheet).toMatchObject({ cantStart: true, suite: 'fast', folder: '/Users/someone/work', rounds: '8' })
    expect(sheet.recipeOptions).toEqual([
      { value: '', label: 'None: write the goal' },
      { value: 'capture-decisions', label: 'capture-decisions' },
      { value: 'changelog-draft', label: 'changelog-draft' },
      { value: 'digest-folder', label: 'digest-folder' },
    ])
    expect(sheet.suiteOptions).toEqual(['cloud', 'fast', 'pair', 'shared', 'vast'])
    await place.tasks.pickRecipe('digest-folder')
    sheet = place.tasks.sheetFace()
    expect(sheet).toMatchObject({ hasRecipe: true, recipeDesc: 'Read a folder and write a digest of it to DIGEST.md.', recipeTakesInput: true, cantStart: true })
    place.tasks.setForm({ input: 'docs' })
    expect(place.tasks.sheetFace().cantStart).toBe(false)
    await place.tasks.pickRecipe('changelog-draft')
    expect(place.tasks.sheetFace().expansion.startsWith("apogee task run 'Here is an excerpt of a git log:")).toBe(true)
    await place.tasks.pickRecipe('')
    expect(place.tasks.sheetFace()).toMatchObject({ hasRecipe: false, cantStart: true })
  })

  test('no apogee: nothing read and nothing drawn, as the old place draws no missing state; a config it cannot read: the CLI’s refusal said in the list', async () => {
    const missing = tasksPlace({ apogee: 'missing' })
    await missing.tasks.appeared()
    expect(missing.child.calls).toEqual([])
    expect(missing.tasks.face()).toMatchObject({ list: [], empty: '', truncation: '', detail: { shown: false } })
    const typo = tasksPlace({ apogee: 'config-typo' })
    await typo.tasks.appeared()
    await settle()
    expect(typo.tasks.face().empty).toMatch(/^The tasks could not be read: apogee task: /)
  })
})

describe('what the place shows before a task is chosen, and a page nobody recorded (design 1p)', () => {
  test('a run of the app’s starting as the place opens is the one shown: the first row is not read meanwhile', async () => {
    const place = tasksPlace({ world: 'sweep' })
    place.tasks.startRun(recordedRun)
    await place.tasks.appeared()
    await place.tick(3)
    expect(place.tasks.face().detail.id).toBe(ids.live)
    expect(callsSince(place.child).filter((call) => call.startsWith('reads task status'))).toEqual([])
    expect(place.child.unanswered).toEqual([])
  })

  test('a row whose task status nobody recorded: its page says the stand-in’s words in place, never an empty page', async () => {
    const place = tasksPlace({ world: 'sweep' })
    await place.tasks.appeared()
    await place.tasks.select('task-20261109-021329-50')
    expect(place.tasks.face().detail.shown).toBe(false)
    expect(place.tasks.face().failure).toBe('no recorded answer for apogee task status task-20261109-021329-50')
    await place.tasks.select(ids.done)
    expect([place.tasks.face().detail.shown, place.tasks.face().failure]).toEqual([true, ''])
  })
})
