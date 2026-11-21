import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { TasksViewModel } from '../ViewModels/TasksViewModel'
import { taskFolder, tasksPlaceRules } from './TasksWorlds'
import type { TasksWorld } from './TasksWorlds'

/** How a preview's place is set up: its world and mode, how many ticks its still frame stands, and what the person does once it has read -- before the clock starts, so the frame's ticks play it. */
export interface TasksPreviewOptions {
  readonly world?: TasksWorld
  readonly apogee?: ApogeeMode
  readonly ticks?: number
  readonly then?: (tasks: TasksViewModel) => void | Promise<void>
  /** What the person does once the frame's ticks have played: Stop after this round, mid-round. */
  readonly after?: (tasks: TasksViewModel) => void | Promise<void>
  /** Ticks played once that act is done: a resume halted as it starts up, on to its `task_started`. */
  readonly afterTicks?: number
}

/**
 * The Tasks place for a preview (design 1p), the Mac app's `TasksPreview`
 * over its documents: the stand-in answering a world's recordings on a
 * still clock, read, the person's act, then the clock started, its ticks
 * playing whatever runs. It names the mode rules' recording itself, as a
 * place's preview may not import `app` (1d).
 */
export function useTasksPreview(options: TasksPreviewOptions = {}): TasksViewModel {
  const [model] = useState(() => {
    const child = new StandInChild({ rules: tasksPlaceRules(options.world ?? 'ledger'), apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ still: true, start: options.ticks ?? 0 })
    const tasks = new TasksViewModel(new ReadRunner(child), new JobStarter(child, clock), () => clock.now)
    tasks.folder = taskFolder
    tasks.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { tasks, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.tasks.appeared()
      await options.then?.(model.tasks)
      model.clock.start()
      await options.after?.(model.tasks)
      if (options.afterTicks !== undefined) model.clock.advance(options.afterTicks)
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.tasks
}
