import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import symphonyPlayErr from '@recorded/business/reads/Fixtures/symphony-play.err?raw'
import { useEffect, useState } from 'react'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import { ConsoleViewModel } from '../ViewModels/ConsoleViewModel'
import { ConsoleView } from '../Views/ConsoleView'
import { consoleRules } from './ConsolePreset'

/**
 * The console on its own (design 1g), one preview per state a person sees
 * differently, its runs answered by recordings: every preset's console
 * answers (`consoleRules`: `config scan`, as the Mac's console previews,
 * `models list` and Tab's words), a symphony's stderr as its busy line, and a
 * run that fails.
 */
function useConsole(lines: readonly string[], options: { readonly typed?: string; readonly complete?: boolean; readonly ticks?: number } = {}): ConsoleViewModel {
  const [state] = useState(() => {
    const rules = consoleRules()
    const child = new StandInChild(
      {
        rules: {
          jobs: [
            ...rules.jobs,
            { arguments: ['symphonies', 'play'], answer: { stderr: symphonyPlayErr, exitAfter: 999 } },
            { arguments: ['models', 'pull'], answer: { stderr: 'apogee models: no such repository\n', exit: 1 } },
          ],
          commands: rules.commands,
        },
        apogee: 'installed',
      },
      { configTypoRefusal: agentsListBadConfig },
    )
    const clock = new Clock({ start: options.ticks ?? 30, still: true })
    const model = new ConsoleViewModel(new ReadRunner(child), new JobStarter(child, clock))
    model.connect(StandInChild.binary)
    return { model, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    state.setUp ??= (async () => {
      for (const line of lines) {
        state.model.setInput(line)
        const ran = state.model.run()
        state.clock.start()
        await Promise.race([ran, new Promise((resolve) => setTimeout(resolve, 0))])
      }
      if (options.typed !== undefined) state.model.setInput(options.typed)
      if (options.complete) await state.model.complete()
    })()
  }, [state, lines, options])
  return state.model
}

function Panel({ model }: { readonly model: ConsoleViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={{ position: 'relative', width: 1184, height: 520, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5, ['--layout-sidebar' as string]: '0px' }}>
      <ConsoleView model={model} close={() => {}} />
    </div>
  )
}

/** Nothing run yet: what the console is. */
export const NothingRun = () => <Panel model={useConsole([])} />

/** `console`: models list's output, apogee reset refused, `models ` typed and its ten words offered. */
export const ThePreset = () => <Panel model={useConsole(['models list', 'reset'], { typed: 'models ', complete: true })} />

/** A command refused, then one run to its end (`config scan`, the Mac's preview). */
export const RefusedThenRun = () => <Panel model={useConsole(['reset', 'config scan'])} />

/** Running: a symphony at its first stage, its stderr the busy line, Stop offered. */
export const Running = () => <Panel model={useConsole(['symphonies play critique-revise --input "A draft README for the tool."'], { ticks: 0 })} />

/** A run that failed: its stderr kept, how it exited. */
export const Failed = () => <Panel model={useConsole(['models pull nope'])} />
