import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import suitesSystem from '@recorded/business/reads/Fixtures/suites-system.json?raw'
import modelsHeld from '../../../../fixtures/business/reads/system-models-held-handmade.json?raw'
import unknown from '../../../../fixtures/business/reads/system-unknown-handmade.json?raw'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { Answer, ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { MonitorViewModel } from '../ViewModels/MonitorViewModel'
import type { MonitorRun } from '../ViewModels/MonitorViewModel'
import { MonitorView } from '../Views/MonitorView'
import { monitorReads } from './MonitorPreset'

/**
 * The monitor on its own (design 1e), one preview per state a person sees
 * differently, each over the stand-in answering `system` with the CLI's
 * recording, or a hand-made file in its exact shape where nobody recorded
 * the state. A place's preview may not import `app`, so it names the mode
 * rules' recording itself (1d).
 */
function useMonitor(answers: readonly Answer[], apogee: ApogeeMode = 'installed', ticks = 0, runs: readonly MonitorRun[] = []) {
  const [built] = useState(() => {
    const child = new StandInChild({ rules: { reads: [{ arguments: ['system'], answer: answers as [Answer, ...Answer[]] }] }, apogee }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ still: true })
    const model = new MonitorViewModel(new ReadRunner(child), clock)
    // The video runs the composition hands in from the shared job list (design 1k), through the same closure.
    if (runs.length > 0) model.running = () => runs
    const found = child.locateBinary()
    model.connect(found.case === 'found' ? found.binary : undefined)
    return { model, clock }
  })
  useEffect(() => {
    built.model.opened()
    built.clock.advance(ticks)
    return () => built.model.closed()
  }, [built, ticks])
  return built.model
}

const frame = { position: 'relative', width: 820, height: 760, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)' } as const

/** The `monitor` preset's run: the recorded reading, its history after the failed read. */
export const Reading = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MonitorView model={useMonitor(monitorReads[0]?.answer as Answer[], 'installed', 15)} close={() => {}} />
  </div>
)

/** The latest read failed: the last value stands, said stale, as the old GPU row says it. */
export const Stale = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MonitorView model={useMonitor([{ stdout: suitesSystem }, { timedOut: true }], 'installed', 5)} close={() => {}} />
  </div>
)

/** The models a process holds, by name, each with Unload (hand-made: no recorded reading holds one). */
export const ModelsHeld = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MonitorView model={useMonitor([{ stdout: modelsHeld }])} close={() => {}} />
  </div>
)

/** Every field unknown, as the CLI prints it where the platform reads nothing (hand-made from its own test's view). */
export const Unknown = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MonitorView model={useMonitor([{ stdout: unknown }])} close={() => {}} />
  </div>
)

/** A clip being made (design 1k): listed from the shared job list by its pipeline and device, with Stop the run, as the old monitor lists `vrun`. */
export const VideoRun = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MonitorView model={useMonitor([{ stdout: suitesSystem }], 'installed', 0, [{ id: 1, name: 'Wan2.2-TI2V-5B · generating', runtime: 'mps · bfloat16' }])} close={() => {}} />
  </div>
)

/** No binary found: the Mac app's words. */
export const NotInstalled = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MonitorView model={useMonitor([{ stdout: suitesSystem }], 'missing')} close={() => {}} />
  </div>
)

/** A config the CLI cannot read: its refusal, word for word. */
export const ConfigTypo = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MonitorView model={useMonitor([{ stdout: suitesSystem }], 'config-typo')} close={() => {}} />
  </div>
)
