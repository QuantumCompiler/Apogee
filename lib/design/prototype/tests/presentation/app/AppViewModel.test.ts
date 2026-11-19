import { describe, expect, test } from 'vitest'
import { presetFor } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel } from '../../../source/presentation/app/ViewModels/AppViewModel'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'

/** Lets every read the start began land. */
const settled = () => new Promise((resolve) => setTimeout(resolve, 0))

/** The app composed at `start`, still, in `apogee`'s mode, and started. */
async function started(start: string | undefined, apogee: ApogeeMode = 'installed') {
  const app = new AppViewModel(presetFor({ ...(start ? { start } : {}), still: true, apogee }).preset, modeRules)
  app.start()
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

  test('opening the file with no start shows the monitor, until 1f ports Sessions', async () => {
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
    app.start()
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
