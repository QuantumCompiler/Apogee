import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import configScan from '@recorded/business/reads/Fixtures/config-scan.txt?raw'
import { describe, expect, test } from 'vitest'
import completeModels from '../../../fixtures/business/reads/complete-models-handmade.txt?raw'
import modelsListText from '../../../fixtures/business/reads/models-list-handmade.txt?raw'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { ConsoleViewModel } from '../../../source/presentation/console/ViewModels/ConsoleViewModel'

const rules: Rules = {
  jobs: [
    { arguments: ['models', 'list'], answer: { stdout: modelsListText } },
    { arguments: ['config', 'scan'], answer: { stdout: configScan, exitAfter: 5 } },
    { arguments: ['models', 'pull'], answer: { stderr: 'apogee models: no such repository\n', exit: 1 } },
  ],
  commands: [
    { arguments: ['__complete', 'models'], answer: { stdout: completeModels } },
    { arguments: ['__complete', 'mod'], answer: { stdout: 'models\n' } },
  ],
}

function console(apogee: ApogeeMode = 'installed') {
  const child = new StandInChild({ rules, apogee }, { configTypoRefusal: agentsListBadConfig })
  const clock = new Clock({ still: false })
  const model = new ConsoleViewModel(new ReadRunner(child), new JobStarter(child, clock))
  const found = child.locateBinary()
  model.connect(found.case === 'found' ? found.binary : undefined)
  return { model, clock, child }
}

const lines = (model: ConsoleViewModel) => model.lines.map((line) => [line.kind, line.text])

describe("the console: any apogee command, as typed (the Mac app's ConsoleViewModel)", () => {
  test("apogee reset, serve and uninstall are refused with the prototype's reasons, and never reach the stand-in", async () => {
    const { model, child } = console()
    for (const line of ['apogee reset', 'serve --port 8080', '--dev uninstall']) {
      model.setInput(line)
      await model.run()
    }
    expect(lines(model)).toEqual([
      ['command', 'apogee reset'],
      ['problem', 'Not run here: deletes the data directory the running app stands on -- run it at a prompt, with the app closed.'],
      ['command', 'apogee serve --port 8080'],
      ['problem', 'Not run here: a daemon the app would orphan when it quits -- run it at a prompt, or under a service manager.'],
      ['command', 'apogee --dev uninstall'],
      ['problem', 'Not run here: removes Apogee and the data directory the app drives -- the app never uninstalls the binary it runs; run it at a prompt, with the app closed.'],
    ])
    expect(child.calls).toEqual([])
    expect(model.history).toEqual(['apogee reset', 'serve --port 8080', '--dev uninstall'])
  })

  test('a line with a recorded answer prints it as the binary did, line by line, then Done.', async () => {
    const { model, clock } = console()
    model.setInput('models list')
    const ran = model.run()
    expect(model.isRunning).toBe(true)
    expect(model.face().running).toMatchObject({ shown: true, busy: 'Running models list' })
    clock.advance(8)
    await ran
    expect(lines(model)).toEqual([
      ['command', 'apogee models list'],
      ...modelsListText
        .trimEnd()
        .split('\n')
        .map((text) => ['out', text]),
      ['note', 'Done.'],
    ])
    expect(model.isRunning).toBe(false)
    expect(model.input).toBe('')
  })

  test('nothing runs while a run is live; Stop ends it and says Stopped.', async () => {
    const { model, clock } = console()
    model.setInput('config scan')
    const ran = model.run()
    clock.advance(2)
    model.setInput('models list')
    expect(model.run()).toBeUndefined()
    await model.stop()
    await ran
    expect(lines(model).at(-1)).toEqual(['note', 'Stopped.'])
    expect(lines(model).filter(([kind]) => kind === 'command')).toEqual([['command', 'apogee config scan']])
  })

  test('a failed run keeps its stderr and says how it exited; a line nobody recorded says it did not start, in the stand-in’s words', async () => {
    const { model, clock } = console()
    model.setInput('models pull nope')
    const pulled = model.run()
    clock.advance(2)
    await pulled
    expect(lines(model).slice(1)).toEqual([
      ['err', 'apogee models: no such repository'],
      ['problem', 'Exited with status 1.'],
    ])
    model.setInput('graph stats')
    await model.run()
    expect(lines(model).at(-1)).toEqual(['problem', 'Didn’t start: no recorded answer for apogee graph stats'])
  })

  test('Clear empties the lines', async () => {
    const { model } = console()
    model.setInput('reset')
    await model.run()
    model.clear()
    expect(model.lines).toEqual([])
    expect(model.face().empty).toBe(true)
  })

  test('Tab asks the binary what comes next (__complete), offering several and completing one', async () => {
    const { model, child } = console()
    model.setInput('models ')
    await model.complete()
    expect(child.calls.at(-1)).toEqual({ kind: 'commands', arguments: ['__complete', 'models', ''] })
    expect(model.suggestions).toEqual(['list', 'info', 'status', 'rename', 'pull', 'delete', 'quantize', 'convert', 'repair', 'migrate'])
    model.accept('info')
    expect(model.input).toBe('models info ')
    expect(model.suggestions).toEqual([])
    model.setInput('apogee mod')
    await model.complete()
    expect(child.calls.at(-1)).toEqual({ kind: 'commands', arguments: ['__complete', 'mod'] })
    expect(model.input, 'one candidate completes the word').toBe('apogee models ')
  })

  test('typing clears the offered words; an answer that lands after the line changed is let go', async () => {
    const { model } = console()
    model.setInput('models ')
    const asked = model.complete()
    model.setInput('models l')
    await asked
    expect(model.suggestions).toEqual([])
    expect(model.input).toBe('models l')
  })

  test('a line that does not split says why in the CLI’s words; a bare apogee asks for a command; with no binary, nothing runs', async () => {
    const { model } = console()
    model.setInput('say "unclosed')
    await model.run()
    model.setInput('apogee')
    await model.run()
    expect(lines(model)).toEqual([
      ['problem', 'A double quote is left open.'],
      ['note', 'Nothing to run: type a command after apogee -- Tab offers them, and --help lists them all.'],
    ])
    const missing = console('missing').model
    missing.setInput('models list')
    await missing.run()
    expect(lines(missing)).toEqual([
      ['command', 'apogee models list'],
      ['problem', 'Apogee isn’t installed, so there is nothing to run.'],
    ])
  })

  test('the line splits as the CLI’s exec line splits it, and the command is read past root options', () => {
    expect(ConsoleViewModel.argumentsOf(`apogee --config ~/x.toml say "a b" c\\ d ''`)).toEqual(['--config', '~/x.toml', 'say', 'a b', 'c d', ''])
    expect(ConsoleViewModel.commandPath(['--config', '~/x.toml', '--dev', 'reset', '--yes'])).toEqual(['reset', '--yes'])
    expect(ConsoleViewModel.refusal(['models', 'list'])).toBeUndefined()
  })
})
