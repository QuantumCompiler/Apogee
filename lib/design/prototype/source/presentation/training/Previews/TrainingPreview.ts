import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { TrainingViewModel } from '../ViewModels/TrainingViewModel'
import type { TrainingTab } from '../ViewModels/TrainingViewModel'
import { trainingWorld } from './TrainingWorlds'
import type { TrainingWorld } from './TrainingWorlds'

/** How a preview's place is set up: its world, its mode, its tab, what the person does once it has read, and how far in a run stands. */
export interface TrainingPreviewOptions {
  readonly world?: TrainingWorld
  readonly apogee?: ApogeeMode
  readonly tab?: TrainingTab
  /** What the person does once the place has read, before the clock starts. */
  readonly then?: (training: TrainingViewModel) => void | Promise<void>
  /** Start ticks: how far in a run, a pass or a pull stands. */
  readonly ticks?: number
  /** What the person does once the clock has started and what it played has landed: Change the promotion… after a refusal. */
  readonly after?: (training: TrainingViewModel) => void | Promise<void>
}

/**
 * The Training place for a preview (design 1j), the Mac app's
 * `TrainingPreview` over its documents: the stand-in answering a world's
 * recordings, a still clock, the place read, the person's act, then the
 * clock started. A place's preview may not import `app`, so it names the
 * mode rules' recording itself (1d).
 */
export function useTrainingPreview(options: TrainingPreviewOptions = {}): TrainingViewModel {
  const [model] = useState(() => {
    const child = new StandInChild({ rules: trainingWorld(options.world ?? 'recorded'), apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ start: options.ticks ?? 0, still: true })
    const training = new TrainingViewModel(new ReadRunner(child), new JobStarter(child, clock))
    training.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    training.pick(options.tab ?? 'runs')
    return { training, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.training.appeared()
      await options.then?.(model.training)
      model.clock.start()
      if (options.after) {
        for (let round = 0; round < 6; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
        await options.after(model.training)
      }
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.training
}
