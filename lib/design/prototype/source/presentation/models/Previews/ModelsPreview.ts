import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { ModelsViewModel } from '../ViewModels/ModelsViewModel'
import { modelsWorld } from './ModelsWorlds'
import type { ModelsWorld } from './ModelsWorlds'

/** How a preview's place is set up: its world, its mode, how far in it stands, and what the person does once it has read. */
export interface ModelsPreviewOptions {
  readonly world?: ModelsWorld
  readonly apogee?: ApogeeMode
  /** Start ticks: how far in a job stands. */
  readonly ticks?: number
  /** What the person does once the place has read, before the clock starts. */
  readonly then?: (models: ModelsViewModel) => void | Promise<void>
}

/**
 * The Models place for a preview (design 1h), the Mac app's
 * `ModelsPreviews` over its documents: the stand-in answering a world's
 * recordings, a still clock, the place read, the person's act, then the
 * clock started. A place's preview may not import `app`, so it names the
 * mode rules' recording itself (1d).
 */
export function useModelsPreview(options: ModelsPreviewOptions = {}): ModelsViewModel {
  const [model] = useState(() => {
    const child = new StandInChild({ rules: modelsWorld(options.world ?? 'store'), apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ start: options.ticks ?? 1, still: true })
    const models = new ModelsViewModel(new ReadRunner(child), new JobStarter(child, clock), clock)
    models.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { models, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.models.appeared()
      await options.then?.(model.models)
      model.clock.start()
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.models
}
