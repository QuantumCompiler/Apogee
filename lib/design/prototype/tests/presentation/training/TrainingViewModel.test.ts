import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import cycleHaltDuringPass from '@recorded/business/reads/Fixtures/train-cycle-halt-during-pass.txt?raw'
import cycleRunHalted from '@recorded/business/jobs/Fixtures/train-cycle-run-halted.jsonl?raw'
import pipelineRun from '@recorded/business/jobs/Fixtures/train-pipeline-run.jsonl?raw'
import promoteHoldsMerged from '@recorded/business/reads/Fixtures/train-promote-holds-merged.err?raw'
import promoteNamesAdapter from '@recorded/business/reads/Fixtures/train-promote-names-adapter.err?raw'
import promoteNoSuite from '@recorded/business/reads/Fixtures/train-promote-adapter-no-suite.err?raw'
import trainRun from '@recorded/business/reads/Fixtures/train-run.err?raw'
import gateFailed from '../../../fixtures/business/reads/train-promote-gate-failed-handmade.err?raw'
import prepareFailed from '../../../fixtures/business/jobs/datasets-prepare-heat-notes-handmade.err?raw'
import { describe, expect, test } from 'vitest'
import { recording, trainingWorld, sweep, promotedBackend } from '../../../source/presentation/training/Previews/TrainingWorlds'
import { PromoteForm, TrainingViewModel } from '../../../source/presentation/training/ViewModels/TrainingViewModel'
import type { Rules } from '../../../source/infrastructure/process/StandInChild'
import { settled, trainingOver } from './TrainingHarness'

const lastLine = (text: string) => text.trim().split('\n').at(-1) ?? ''
const scheduler = 'your scheduler’s'

describe('the Training place reads the CLI (design 1j, the Mac’s TrainingViewModel)', () => {
  test("the recorded world: Runs as the old prototype draws it, from train status, train versions, the regimes, the kits and the datasets", async () => {
    const { training, child } = await trainingOver('recorded')
    expect(child.calls.filter((call) => call.kind === 'reads').map((call) => call.arguments.join(' '))).toEqual([
      'train status',
      'train pipeline list',
      'train pipeline status',
      'train versions',
      'train regime list',
      'datasets kits',
      'datasets list',
    ])
    const face = training.face()
    expect(face.tab).toBe('runs')
    expect(face.tabs.map((tab) => tab.label)).toEqual(['Runs', 'Pipelines', 'Cycle', 'Regimes', 'Datasets'])
    expect(face.runs.rows).toEqual([{ id: '20261021-051826', status: 'complete', meta: 'lora · mock · loss 0.4200 · failed the gate (46%) -- promote refuses it unless forced', failed: true, current: true }])
    expect(face.runs.detail).toEqual({
      shown: true,
      id: '20261021-051826',
      gate: 'failed the gate (46%) -- promote refuses it unless forced',
      failed: true,
      facts: [
        { label: 'TRAINED', value: '/Users/someone/work/student' },
        { label: 'ON', value: '/Users/someone/work/data.jsonl' },
        { label: 'HOW', value: 'lora · mock · 4 iterations' },
        { label: 'FINAL LOSS', value: '0.4200' },
        { label: 'RAN', value: '2026-10-21T05:18:26Z → 2026-10-21T05:18:26Z' },
      ],
    })
    expect(face.runs.versions).toEqual([
      { backend: 'based', canRoll: false, lines: ['v1 · run 20261026-065637 · adapter · active'] },
      { backend: 'q4', canRoll: false, lines: ['v1 · run 20261026-065638 · adapter · scored on tiny-Q4_K_M.gguf (Q4_K_M) · active'] },
      { backend: 'tuned', canRoll: false, lines: ['v1 · run 20261026-065637 · active'] },
    ])
    expect(face.regimes.rows.map((row) => [row.name, row.meta])).toEqual([
      ['foundation', '4 kits · seeded'],
      ['harness-helpers', '3 kits · seeded'],
      ['knowledge', '1 kit · seeded'],
    ])
    expect(face.regimes.detail).toEqual({ shown: true, name: 'foundation', description: 'The four general kits as one gated pipeline -- instruction-following, structured output, summarization, then reasoning.', kits: ['1. instruction-following', '2. structured-output', '3. summarization', '4. reasoning'] })
    expect(face.datasets.rows).toEqual([{ name: 'heat-notes', meta: '2 examples · chat', current: true }])
    expect(face.datasets.detail).toEqual({ shown: true, name: 'heat-notes', line: '2 examples · chat · 449 bytes', path: '/Users/someone/.apogee/training/datasets/heat-notes.jsonl' })
    expect(face.datasets.kits.at(-1)).toEqual({ name: 'utility-chores', meta: '24 eval items' })
    expect(face.pipelines.noSpecs).toBe("no named pipelines -- add one under training.pipelines, or run a spec file with 'apogee train pipeline run --pipeline <file>'")
    expect(face.pipelines.noRuns).toBe("no pipeline runs yet ('apogee train pipeline run --pipeline <spec>')")
    expect(face.cycle.noHistory).toBe(`No pass has run yet, so the cycle has no history: its first pass writes one -- Run one pass here, or ${scheduler} first run. A pass needs training.cycle in your config; the run says so when it’s missing.`)
    expect(face.header.newRun).toEqual({ shown: true, enabled: true })
    expect(face.bar.shown).toBe(false)
  })

  test("with no apogee: Runs and Datasets say it isn't installed, as the old ones do; nothing is read; Pipelines, Cycle and Regimes have no state of their own", async () => {
    const { training, child } = await trainingOver('recorded', {}, 'missing')
    expect(child.calls).toEqual([])
    const face = training.face()
    expect(face.runs.missing).toBe("Apogee isn't installed, so there is nothing to train.")
    expect(face.runs.detail.shown).toBe(false)
    expect(face.runs.noneChosen).toBe('Choose a run to see what it trained and how it did.')
    expect(face.runs.versionsShown).toBe(false)
    expect(face.datasets.missing).toBe("Apogee isn't installed, so there is nothing to train.")
    expect(face.datasets.noneChosen).toBe('Choose a dataset.')
    expect(face.datasets.kits).toEqual([])
    expect(face.header.newRun.enabled).toBe(false)
    expect(face.header.datasets.enabled).toBe(false)
    expect(face.header.cycle.canRun).toBe(false)
    expect(face.header.cycle.canHalt, 'no binary: nothing to halt with').toBe(false)
    expect(face.pipelines.noSpecs).not.toBe('')
    expect(face.regimes.rows).toEqual([])
    expect(face.regimes.detail.shown).toBe(false)
    expect(face.cycle.noHistory).not.toBe('')
  })

  test('a recording no import holds is refused, naming it', () => {
    expect(() => recording('train-cycle-status-invented.json')).toThrow('no fixture named train-cycle-status-invented.json')
  })

  test('a config the CLI refuses: each list says its read failed, in the CLI’s words', async () => {
    const { training } = await trainingOver('recorded', {}, 'config-typo')
    const refusal = lastLine(agentsListBadConfig).replace('apogee agents:', 'apogee train:')
    expect(training.face().runs.failure).toBe(`The runs could not be read: ${refusal} (exited with status 1)`)
    expect(training.face().datasets.failure).toBe(`The datasets could not be read: ${lastLine(agentsListBadConfig).replace('apogee agents:', 'apogee datasets:')} (exited with status 1)`)
  })
})

describe('New run…: `train run` replayed, its stderr the busy line (the `training` preset)', () => {
  test("the current run's base and data trained again, the bar its busy line, the old frame's at four ticks", async () => {
    const { training, child, advance, said, registry } = await trainingOver('recorded')
    training.newRun()
    expect(child.calls.at(-1)?.arguments).toEqual(['train', 'run', '/Users/someone/work/student', '--dataset', '/Users/someone/work/data.jsonl'])
    expect(training.face().bar).toEqual({ shown: true, label: 'Training', line: 'Starting…', canStop: true })
    expect(training.face().header.newRun.enabled).toBe(false)
    expect(registry.children('job').map((job) => job.label)).toEqual(['apogee train run /Users/someone/work/student'])
    await advance(4)
    expect(training.face().bar.line).toBe('[train] iter 2/4 · loss 0.6200 · lr 1.00e-04 · 10.0 it/s')
    await advance(4)
    expect(training.face().bar.shown).toBe(false)
    expect(said).toEqual(['Training: done'])
    expect(registry.children('job')).toEqual([])
    expect(trainRun).toContain('[train] iter 2/4')
  })

  test('Stop ends it, said as the Mac says it', async () => {
    const { training, advance, said } = await trainingOver('recorded')
    training.newRun()
    await advance(2)
    await training.stop()
    await settled()
    expect(training.face().bar.shown).toBe(false)
    expect(said).toEqual(['Stopped: what was finished stays'])
  })
})

describe('the replays: a pipeline run, its resume, a cycle pass, a halt and a resume, each from its recorded stream', () => {
  test("a pipeline run aborts at stage 2 and offers \"Resume from stage 2, refusals\"; the resume completes", async () => {
    const { training, asked, said, advance, child } = await trainingOver('pipelines')
    training.pick('pipelines')
    expect(training.face().pipelines.specs).toEqual([
      { name: 'nightly', meta: '1 stage · student tiny', command: 'apogee train pipeline run --pipeline nightly', label: 'Run the pipeline nightly', canRun: true },
      { name: 'skills', meta: '2 stages · student tiny', command: 'apogee train pipeline run --pipeline skills', label: 'Run the pipeline skills', canRun: true },
    ])
    training.requestRun('skills')
    expect(asked).toEqual(['train'])
    expect(training.confirmWords('train')).toEqual({ title: 'Run the pipeline skills?', body: 'Each stage is a training run, held to the cumulative gate -- this machine is held until it ends.', detail: 'apogee train pipeline run --pipeline skills', confirm: 'Start', cancel: 'Cancel' })
    await training.confirm('train')
    expect(child.calls.at(-1)?.arguments).toEqual(['train', 'pipeline', 'run', '--pipeline', 'skills', '--output-format', 'stream-json'])
    // Until its stream names the run, the row and the page say the old bar's word.
    expect([training.face().pipelines.live.id, training.face().pipelines.page.id]).toEqual(['Starting…', 'Starting…'])
    await advance(24)
    const live = training.face().pipelines
    expect(live.live).toEqual({ shown: true, id: 'pipe-20261026-074112', current: true })
    expect(live.page.kind).toBe('live')
    expect(live.page.stages.map((stage) => [stage.index, stage.name, stage.line, stage.runLine])).toEqual([
      ['1', 'greetings', 'passed · cumulative 100%', 'run pipe-20261026-074112-s0'],
      ['2', 'refusals', 'training · iter 2/4 · loss 0.6200 · 10.0 it/s', 'run pipe-20261026-074112-s1'],
    ])
    expect(training.face().bar).toEqual({ shown: true, label: 'Running skills', line: '[train] mock trainer: 4 iteration(s), method lora', canStop: true })
    await advance(10)
    const ended = training.face().pipelines
    expect(training.face().bar.shown).toBe(false)
    expect(said.at(-1)).toBe("pipeline aborted -- stage 'refusals' failed the cumulative eval gate (50% (1/2)). Fix the data or the suite and 'apogee train pipeline resume pipe-20261026-074112', or pass --continue-on-fail to go on regardless")
    expect(ended.page.kind).toBe('run')
    expect(ended.page.id).toBe('pipe-20261026-074112')
    expect([ended.page.line, ended.page.failed]).toEqual(['skills · aborted · on tiny', true])
    expect(ended.page.stages.map((stage) => stage.line)).toEqual(['passed · cumulative 100%', 'failed · cumulative 50%'])
    expect(ended.page.resume).toEqual({ shown: true, line: 'Resume from stage 2, refusals', command: 'apogee train pipeline resume pipe-20261026-074112: from the first stage that has not passed', enabled: true, hold: '' })
    expect(ended.page.endLine).toBe('Each stage’s run is an ordinary run: gate it and promote it from Runs.')
    expect(ended.rows).toEqual([{ id: 'pipe-20261026-074112', line: 'skills · aborted · 1 of 2 stages passed', failed: true, current: true }])

    training.requestResume()
    expect(training.confirmWords('train')).toEqual({
      title: 'Resume pipe-20261026-074112 from stage 2, refusals?',
      body: 'What passed stays; each stage from there is a training run, held to the cumulative gate -- this machine is held until it ends.',
      detail: 'apogee train pipeline resume pipe-20261026-074112',
      confirm: 'Resume',
      cancel: 'Cancel',
    })
    await training.confirm('train')
    expect(training.face().bar.label).toBe('Resuming pipe-20261026-074112')
    // Its first writes repeat stage 2's last attempt's score; while it trains again, that score is not this attempt's gate.
    await advance(9)
    expect(training.face().pipelines.page.stages.map((stage) => stage.line)).toEqual(['passed · cumulative 100%', 'training · iter 2/4 · loss 0.6200 · 10.0 it/s'])
    await advance(11)
    const done = training.face().pipelines
    expect(said.at(-1)).toBe('Pipeline pipe-20261026-074112: complete')
    expect([done.page.line, done.page.failed, done.page.resume.shown]).toEqual(['skills · complete · on tiny', false, false])
    expect(done.page.when).toBe('2026-10-26T07:41:12Z → 2026-10-26T07:41:13Z')
    expect(done.rows).toEqual([{ id: 'pipe-20261026-074112', line: 'skills · complete · 2 of 2 stages passed', failed: false, current: true }])
  })

  test('a cycle pass ends skipped, then passed, then failed -- halting the cycle -- each from its recorded stream; Resume clears it', async () => {
    const { training, said, advance } = await trainingOver('pipelines')
    training.pick('cycle')
    const pass = async (ticks: number) => {
      training.requestPass()
      expect(training.confirmWords('train')).toEqual({ title: 'Run the training cycle once?', body: 'It trains, gates and may promote -- this machine is held until it ends.', detail: 'apogee train cycle run', confirm: 'Run one pass', cancel: 'Cancel' })
      await training.confirm('train')
      await advance(ticks)
    }
    await pass(1)
    expect(training.face().cycle.live).toMatchObject({ shown: true, starting: true, lines: [] })
    await advance(1)
    expect(training.face().cycle.live).toMatchObject({ shown: true, starting: false, lines: [{ state: 'collecting', text: '[cycle] directory: no new files in /Users/someone/work/queue', latest: true }] })
    await advance(3)
    expect(said.at(-1)).toBe('[cycle] no new data -- skipped')
    expect(training.face().cycle.passes.map((row) => [row.at, row.line])).toEqual([['2026-10-26T07:41:18Z', 'skipped']])
    expect(training.face().cycle.passes[0]?.reason).toBe('no new training data in any source')

    await pass(32)
    expect(said.at(-1)).toBe('[cycle] complete -- nightly-model promoted to v1 (/Users/someone/.apogee/models/tiny/gguf/052b295c3e9b/nightly-model-v1.gguf)')
    expect(training.face().cycle.facts).toEqual([
      { label: 'PROMOTES INTO', value: 'nightly-model' },
      { label: 'ANCHOR', value: 'v1 (100%)' },
      { label: 'STATE', value: 'idle' },
      { label: 'FAILED IN A ROW', value: '0' },
      { label: 'PASSES', value: '2' },
    ])
    expect(training.face().cycle.passes.map((row) => row.line)).toEqual(['pass · scored 100% · promoted v1 · 2 examples from directory', 'skipped'])

    await pass(23)
    expect(said.at(-1)).toBe("cycle FAILED -- pipeline aborted: pipeline aborted -- stage 'base' failed the cumulative eval gate (0% (0/1)). Fix the data or the suite and 'apogee train pipeline resume cycle-20261026-074120', or pass --continue-on-fail to go on regardless")
    const halted = training.face()
    expect(halted.cycle.hold).toBe(`Halted: circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1. Every pass refuses -- ${scheduler} too -- until Resume clears it.`)
    expect(halted.header.cycle).toEqual({ shown: true, canRun: false, halted: true, canHalt: false })
    expect(halted.cycle.facts.find((fact) => fact.label === 'STATE')?.value).toBe('halted')
    expect(halted.cycle.passes[0]).toMatchObject({ line: 'fail · 1 example from directory', failed: true, note: 'circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1', pipeline: 'cycle-20261026-074120' })

    training.requestResumeCycle()
    expect(training.confirmWords('train')).toEqual({ title: 'Resume the training cycle?', body: `It clears the halt and the failure count: ${scheduler} next pass runs.`, detail: 'apogee train cycle resume', confirm: 'Resume', cancel: 'Cancel' })
    await training.confirm('train')
    expect(said.at(-1)).toBe('cycle resumed (was halted: circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1); the failure count is reset')
    expect(training.face().header.cycle).toEqual({ shown: true, canRun: true, halted: false, canHalt: true })
    expect(training.face().cycle.hold).toBe('')
    expect(training.face().cycle.facts.find((fact) => fact.label === 'FAILED IN A ROW')?.value).toBe('0')
  })

  test('a halt taken during a pass keeps the halt: the pass finishes, says so, and the cycle stays halted (train-cycle-run-halted.jsonl)', async () => {
    const base = trainingWorld('cycle')
    const rules: Rules = {
      ...base,
      commands: [...(base.commands ?? []), { arguments: ['train', 'cycle', 'halt'], answer: { stdout: cycleHaltDuringPass } }],
      jobs: [...(base.jobs ?? []), { arguments: ['train', 'cycle', 'run'], answer: { stdout: cycleRunHalted } }],
    }
    const { training, said, advance } = await trainingOver(rules)
    training.pick('cycle')
    training.requestPass()
    await training.confirm('train')
    await advance(10)
    expect(training.face().header.cycle.canHalt).toBe(true)
    training.requestHalt()
    expect(training.confirmWords('train')).toEqual({
      title: 'Halt the training cycle?',
      body: `Every 'cycle run' refuses until 'cycle resume' -- ${scheduler} passes too. The pass running now finishes, and keeps the halt.`,
      detail: 'apogee train cycle halt',
      confirm: 'Halt',
      cancel: 'Cancel',
    })
    await training.confirm('train')
    expect(said.at(-1)).toBe('the pass running now finishes, and keeps the halt')
    expect(training.face().cycle.hold).toBe(`Halted: halted by 'apogee train cycle halt'. The pass running now finishes, and keeps the halt: every pass after it refuses -- ${scheduler} too -- until Resume clears it.`)
    expect(training.face().cycle.facts.find((fact) => fact.label === 'STATE')?.value).toBe('a pass is running')
    await advance(230)
    expect(training.face().bar.shown).toBe(false)
    expect(said.at(-1)).toBe("[cycle] halted while this pass ran (halted by 'apogee train cycle halt') -- the halt is kept: 'apogee train cycle run' refuses until 'apogee train cycle resume'")
    expect(training.face().header.cycle.halted).toBe(true)
    expect(training.face().cycle.hold).toBe(`Halted: halted by 'apogee train cycle halt'. Every pass refuses -- ${scheduler} too -- until Resume clears it.`)
  })

  const kept = "[cycle] halted while this pass ran (halted by 'apogee train cycle halt') -- the halt is kept: 'apogee train cycle run' refuses until 'apogee train cycle resume'"
  const byHand = `Halted: halted by 'apogee train cycle halt'. Every pass refuses -- ${scheduler} too -- until Resume clears it.`
  /** Run one pass, and halt it `after` ticks in; then let it end. */
  const haltDuring = async (harness: Awaited<ReturnType<typeof trainingOver>>, after: number) => {
    const { training, advance } = harness
    training.requestPass()
    await training.confirm('train')
    await advance(after)
    expect(training.face().bar.shown, 'the pass is still running when the halt is taken').toBe(true)
    training.requestHalt()
    await training.confirm('train')
    await advance(100)
    expect(training.face().bar.shown).toBe(false)
  }

  test("a halt taken during this place's pass on a preset's world is kept when the pass ends: the pass's history with the halt on top, the CLI's kept notice said (the skipped pass)", async () => {
    const harness = await trainingOver('pipelines')
    harness.training.pick('cycle')
    await haltDuring(harness, 1)
    const face = harness.training.face()
    expect(face.header.cycle).toEqual({ shown: true, canRun: false, halted: true, canHalt: false })
    expect(face.cycle.hold).toBe(byHand)
    expect(face.cycle.facts.find((fact) => fact.label === 'STATE')?.value).toBe('halted')
    expect(face.cycle.passes.map((row) => row.line)).toEqual(['skipped'])
    expect(face.cycle.keptHalt).toBe(kept)
    expect(harness.said.at(-1)).toBe(kept)
  })

  test("…and the passed pass after it: promoted, anchored, and still halted by hand", async () => {
    const harness = await trainingOver('pipelines')
    harness.training.pick('cycle')
    harness.training.requestPass()
    await harness.training.confirm('train')
    await harness.advance(5)
    expect(harness.said.at(-1)).toBe('[cycle] no new data -- skipped')
    await haltDuring(harness, 10)
    const face = harness.training.face()
    expect(face.header.cycle).toEqual({ shown: true, canRun: false, halted: true, canHalt: false })
    expect(face.cycle.hold).toBe(byHand)
    expect(face.cycle.facts.find((fact) => fact.label === 'ANCHOR')?.value).toBe('v1 (100%)')
    expect(face.cycle.passes.map((row) => row.line)).toEqual(['pass · scored 100% · promoted v1 · 2 examples from directory', 'skipped'])
    expect(face.cycle.keptHalt).toBe(kept)
    expect(harness.said.at(-1)).toBe(kept)
  })

  test("…and the failing pass on training:cycle's world: the halt by hand stands over the breaker's, the pass's failure said, the kept notice on the page", async () => {
    const harness = await trainingOver('cycle')
    harness.training.pick('cycle')
    await haltDuring(harness, 10)
    const face = harness.training.face()
    expect(face.header.cycle).toEqual({ shown: true, canRun: false, halted: true, canHalt: false })
    expect(face.cycle.hold).toBe(byHand)
    expect(face.cycle.keptHalt).toBe(kept)
    expect(harness.said.at(-1)).toBe("cycle FAILED -- pipeline aborted: pipeline aborted -- stage 'base' failed the cumulative eval gate (0% (0/1)). Fix the data or the suite and 'apogee train pipeline resume cycle-20261026-074120', or pass --continue-on-fail to go on regardless")
  })

  test("a halt by hand, then Resume, on the recorded world: the CLI's line for a halt by hand, never the breaker's (train-cycle-resume-after-kept.txt)", async () => {
    const { training, said } = await trainingOver('recorded')
    training.pick('cycle')
    training.requestHalt()
    await training.confirm('train')
    expect(training.face().cycle.hold).toBe(byHand)
    training.requestResumeCycle()
    await training.confirm('train')
    expect(said.at(-1)).toBe("cycle resumed (was halted: halted by 'apogee train cycle halt'); the failure count is reset")
    expect(training.face().header.cycle).toEqual({ shown: true, canRun: true, halted: false, canHalt: true })
  })

  test("training:passrunning's world: a pass always holds the home, so Halt… says the CLI's during-pass line (train-cycle-halt-during-pass.txt)", async () => {
    const { training, said } = await trainingOver('passrunning')
    training.pick('cycle')
    training.requestHalt()
    await training.confirm('train')
    expect(said.at(-1)).toBe('the pass running now finishes, and keeps the halt')
    expect(training.face().cycle.hold).toContain(`Halted: halted by 'apogee train cycle halt'. The pass running now finishes, and keeps the halt: every pass after it refuses -- ${scheduler} too -- until Resume clears it.`)
  })

  test("a pipeline's end says its own run's status, whatever page is open (nightly completes while the aborted skills run's page is open)", async () => {
    const { training, said, advance } = await trainingOver('pipelines')
    training.pick('pipelines')
    training.requestRun('nightly')
    await training.confirm('train')
    await advance(3)
    await training.openPipelineRun('pipe-20261026-074112')
    expect(training.face().pipelines.page).toMatchObject({ kind: 'run', id: 'pipe-20261026-074112', line: 'skills · aborted · on tiny' })
    await advance(20)
    expect(training.face().bar.shown).toBe(false)
    expect(said.at(-1)).toBe('Pipeline nightly: complete')
    expect(training.face().pipelines.rows.find((row) => row.id === 'pipe-20261026-074115')?.line).toBe('nightly · complete · 1 of 1 stage passed')
  })

  test('Stop during a pipeline run: said as the Mac says it, its run page the CLI’s read of it', async () => {
    const { training, said, advance } = await trainingOver('pipelines')
    training.pick('pipelines')
    training.requestRun('skills')
    await training.confirm('train')
    await advance(24)
    await training.stop()
    await settled()
    expect(said.at(-1)).toBe('Stopped: what passed stays -- Resume continues it')
    expect(training.face().pipelines.page.id).toBe('pipe-20261026-074112')
    expect(training.face().pipelines.page.resume.line).toBe('Resume from stage 2, refusals')
    expect(pipelineRun).toContain('pipe-20261026-074112')
  })
})

describe('the promote sheet: each refusal word for word, and Change the promotion… in the mode it names', () => {
  const sheetOf = (training: TrainingViewModel) => training.face().promoteSheet

  test("the sheet's form: merged by default, its command built as the CLI takes it, Promote off until a backend is named", async () => {
    const { training, sheets } = await trainingOver('recorded')
    training.newPromotion()
    expect(sheets).toEqual(['promote'])
    const sheet = sheetOf(training)
    expect(sheet.title).toBe('Promote 20261021-051826')
    expect(sheet.verdict).toEqual({ text: 'This run failed the gate (46%) -- promote refuses it unless forced.', failed: true })
    expect([sheet.adapter, sheet.command, sheet.canPromote]).toEqual([false, 'apogee train promote 20261021-051826 --as <backend>', false])
    expect(sheet.entries).toEqual(['based', 'q4', 'tuned'])
    training.setPromote({ adapter: true, backend: 'q4', suite: ' s.jsonl ', judge: 'claude', trainer: 'mock', force: true })
    expect(sheetOf(training).command).toBe('apogee train promote 20261021-051826 --as q4 --adapter --suite s.jsonl --judge claude --trainer mock --force')
    expect(sheetOf(training).ledger).toBe('q4 holds adapter versions (active v1 of 1)')
    training.setPromote({ adapter: false, target: 'gguf', quantize: 'Q4_K_M', keepFused: true })
    expect(sheetOf(training).command).toBe('apogee train promote 20261021-051826 --as q4 --target gguf --quantize Q4_K_M --keep-fused --trainer mock --force')
  })

  const refusal = async (change: Parameters<TrainingViewModel['setPromote']>[0], run?: string) => {
    const harness = await trainingOver('recorded')
    harness.training.newPromotion(run)
    harness.training.setPromote(change)
    harness.training.promote()
    await harness.advance(2)
    return harness
  }

  test('unforced, the failed run is refused at its gate: the hand-made line from promote.cpp:75 and :45; Change the promotion… keeps the mode', async () => {
    const { training, asked, sheets } = await refusal({ backend: 'based' })
    expect(asked).toEqual(['promoteRefused'])
    expect(training.confirmWords('promoteRefused')).toEqual({ title: "That didn't work", body: gateFailed.trim(), detail: undefined, confirm: 'Change the promotion…', cancel: 'OK' })
    training.changePromotion()
    expect(sheets.at(-1)).toBe('promote')
    expect(sheetOf(training)).toMatchObject({ error: gateFailed.trim(), adapter: false, backend: 'based' })
  })

  test('forced, merged into based: train-promote-names-adapter.err, and Change the promotion… opens the adapter mode it names', async () => {
    const { training } = await refusal({ backend: 'based', force: true })
    expect(training.confirmWords('promoteRefused')?.body).toBe(promoteNamesAdapter.trim())
    training.changePromotion()
    expect(sheetOf(training)).toMatchObject({ error: promoteNamesAdapter.trim(), adapter: true, backend: 'based', force: true })
  })

  test('forced, the adapter onto tuned: train-promote-holds-merged.err, and Change the promotion… opens the merge it names', async () => {
    const { training } = await refusal({ adapter: true, backend: 'tuned', force: true })
    expect(training.confirmWords('promoteRefused')?.body).toBe(promoteHoldsMerged.trim())
    training.changePromotion()
    expect(sheetOf(training)).toMatchObject({ error: promoteHoldsMerged.trim(), adapter: false })
  })

  test("q4's run, its adapter with no suite: train-promote-adapter-no-suite.err, the mode kept", async () => {
    const { training } = await refusal({ adapter: true, backend: 'q4' }, '20261026-065638')
    expect(training.confirmWords('promoteRefused')?.body).toBe(promoteNoSuite.trim())
    training.changePromotion()
    expect(sheetOf(training)).toMatchObject({ adapter: true, backend: 'q4' })
  })

  test('OK lets the refusal go; a promote the CLI did: its [promote] lines the busy line, its first line said', async () => {
    const { training, said, advance } = await trainingOver('recorded')
    training.newPromotion()
    training.setPromote({ backend: promotedBackend, force: true })
    training.promote()
    await advance(1)
    expect(training.face().bar).toEqual({ shown: true, label: 'Promoting', line: "apogee train: warning: promoting despite the gate (--force): the eval gate failed for run 20261021-051826 (46%: 11/24 passed). Fix the training or the suite, then re-run eval", canStop: true })
    await advance(2)
    expect(training.face().bar.line).toBe('[promote] fusing the adapter into the base model')
    await advance(20)
    expect(said.at(-1)).toBe('promoted to new backend student-tuned -> v1')
    expect(new PromoteForm().arguments()).toBeUndefined()
  })
})

describe('the pull sheet, the pulled banner and Prepare it', () => {
  test('a pull streams (M30): its bytes on the bar, then the banner from its document; Prepare it runs datasets prepare, which the CLI refuses on that folder', async () => {
    const { training, said, advance, child, sheets } = await trainingOver('recorded')
    training.pick('runs')
    training.newPull()
    expect(sheets).toEqual(['pull'])
    expect(training.face().pullSheet).toEqual({ ref: '', command: 'apogee datasets pull owner/name', canPull: false })
    training.setPullRef(' someone/heat-notes ')
    expect(training.face().pullSheet).toEqual({ ref: ' someone/heat-notes ', command: 'apogee datasets pull someone/heat-notes', canPull: true })
    training.pull()
    expect(sheets.at(-1)).toBe('dismissed')
    expect(training.face().tab).toBe('datasets')
    expect(child.calls.at(-1)?.arguments).toEqual(['datasets', 'pull', 'someone/heat-notes', '--output-format', 'stream-json'])
    expect(training.face().header.datasets.canPull).toBe(false)
    await advance(3)
    expect(training.face().bar).toEqual({ shown: true, label: 'Pulling', line: 'downloading 2 file(s), 136 KiB, into /Users/someone/.apogee/training/datasets/raw/someone--heat-notes', canStop: true })
    await advance(14)
    expect(said.at(-1)).toBe('Pulled: 2 file(s), 136 KiB')
    expect(training.face().datasets.pulled).toEqual({ shown: true, line: 'Pulled: 2 file(s), 136 KiB', path: '/Users/someone/.apogee/training/datasets/raw/someone--heat-notes', command: 'apogee datasets prepare /Users/someone/.apogee/training/datasets/raw/someone--heat-notes: the files made trainer-ready', canPrepare: true })

    training.prepare()
    expect(child.calls.at(-1)?.arguments).toEqual(['datasets', 'prepare', '/Users/someone/.apogee/training/datasets/raw/someone--heat-notes'])
    await advance(1)
    expect(training.face().bar).toEqual({ shown: true, label: 'Preparing', line: "[prepare] Loading dataset from /Users/someone/.apogee/training/datasets/raw/someone--heat-notes (split: 'train')...", canStop: true })
    await advance(3)
    expect(said.at(-1)).toBe(lastLine(prepareFailed))
    expect(training.face().datasets.pulled.shown).toBe(true)
    training.dismissPulled()
    expect(training.face().datasets.pulled.shown).toBe(false)
  })
})

describe("GUI 6i's states over 57g's and 57h's recordings", () => {
  test("a dead pass's lock: said with its path and remedy, Run one pass held, Halt still offered", async () => {
    const { training } = await trainingOver('stalelock')
    training.pick('cycle')
    const cycle = training.face().cycle
    expect(cycle.hold).toBe(`Stopped -- cycle.lock is left by a process that is not running; remove /Users/someone/.apogee/training/cycle/cycle.lock to run again. Until then every pass refuses -- Run one pass here, and ${scheduler}.`)
    expect(cycle.lock).toBe('/Users/someone/.apogee/training/cycle/cycle.lock')
    expect(cycle.facts.find((fact) => fact.label === 'STATE')?.value).toBe('stopped, its lock left behind')
    expect(training.face().header.cycle).toEqual({ shown: true, canRun: false, halted: false, canHalt: true })
    expect(cycle.passes[0]?.line).toBe('fail · 1 example from directory')
  })

  test('an unreadable history: said in its place, the three controls held', async () => {
    const { training, child } = await trainingOver('unreadable')
    training.pick('cycle')
    expect(child.calls.some((call) => call.arguments.join(' ') === 'train cycle status')).toBe(false)
    const cycle = training.face().cycle
    expect(cycle.error).toBe('The cycle history could not be read: /Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array')
    expect(cycle.errorHeld).toBe('Run one pass, Halt and Resume are held: each refuses while the history cannot be read.')
    expect(cycle.noHistory).toBe('')
    expect(training.face().header.cycle).toEqual({ shown: true, canRun: false, halted: false, canHalt: false })
  })

  test("a cycle pass's page: never offered Resume, its line 57h's, Show the cycle its door", async () => {
    const { training } = await trainingOver('stalelock')
    await training.openPipelineRun(sweep.cyclePass)
    const page = training.face().pipelines.page
    expect([page.line, page.resume.shown, page.isCyclePass]).toEqual(['cyclic · aborted · on tiny', false, true])
    expect(page.endLine).toBe('A cycle pass: its candidate was discarded, and no other verb resumes it -- Run one pass on the Cycle tab runs the next pass.')
    training.showCycle()
    expect(training.face().tab).toBe('cycle')
  })

  test('the cycle pass mid-pass, another process holding it: it runs and the cycle judges it; the cycle held', async () => {
    const { training } = await trainingOver('passrunning')
    await training.openPipelineRun(sweep.cyclePass)
    expect(training.face().pipelines.page.endLine).toBe('A cycle pass, running now (process 67959): its candidate is the cycle’s, judged by its dual gate when the pass ends -- never from Runs.')
    expect(training.face().pipelines.page.stages.map((stage) => stage.line)).toEqual(['training'])
    expect(training.face().cycle.facts.find((fact) => fact.label === 'STATE')?.value).toBe('a pass is running')
    expect(training.face().header.cycle.canRun).toBe(false)
  })

  test('a resume a live process holds, in its words; one whose spec file is gone; one that keeps an earlier failed stage', async () => {
    const held = await trainingOver('resumeheld')
    await held.training.openPipelineRun(sweep.held)
    expect(held.training.face().pipelines.page.resume).toEqual({
      shown: true,
      line: 'Resume from stage 2, b',
      command: 'apogee train pipeline resume pipe-20261109-040503 --pipeline nightly: after the last stage that passed',
      enabled: false,
      hold: 'This pipeline is running now (process 67959); a resume is refused while it runs.',
    })
    expect(held.training.face().pipelines.page.line).toBe('Nightly SFT · aborted · on tiny')
    const { training } = await trainingOver('stalelock')
    await training.openPipelineRun(sweep.specFileGone)
    expect(training.face().pipelines.page.resume).toMatchObject({ enabled: false, hold: "Resume needs its spec: run 'apogee train pipeline resume pipe-20261109-041444 --pipeline <spec>' in the console." })
    await training.openPipelineRun(sweep.keptFailed)
    expect(training.face().pipelines.page.resume).toMatchObject({ line: 'Resume from stage 3, c; stage 1, a, failed its gate and stays as it is', enabled: true, hold: '' })
    training.requestResume()
    expect(training.confirmWords('train')).toMatchObject({ title: 'Resume pipe-20261109-041458 from stage 3, c?', body: 'Stage 1, a, failed its gate and stays as it is. What passed stays; each stage from there is a training run, held to the cumulative gate -- this machine is held until it ends.', detail: 'apogee train pipeline resume pipe-20261109-041458 --pipeline three' })
  })
})

describe("the prototype's own acts, said as toasts ending (prototype)", () => {
  test('Gate it, Run this regime, New dataset…, Synthesize…, Prepare a file… and Delete…', async () => {
    const { training, said } = await trainingOver('recorded')
    training.gate()
    training.runRegime()
    training.newDataset()
    training.synthesize()
    training.prepareFile()
    training.deleteDataset()
    expect(said).toEqual([
      'apogee train eval 20261021-051826 (prototype)',
      'apogee train regime run foundation (prototype)',
      'apogee datasets create <name> --from template (prototype)',
      'apogee datasets synth <name> --teacher <backend> --kit <kit> (prototype)',
      'apogee datasets prepare <file> (prototype)',
      'apogee datasets delete heat-notes --yes: confirmed first (prototype)',
    ])
  })

  test('a regime chosen shows its kits in order', async () => {
    const { training } = await trainingOver('recorded')
    training.selectRegime('knowledge')
    expect(training.face().regimes.detail).toMatchObject({ name: 'knowledge', kits: ['1. knowledge-capture'] })
    expect(training.face().regimes.rows.find((row) => row.current)?.name).toBe('knowledge')
  })
})
