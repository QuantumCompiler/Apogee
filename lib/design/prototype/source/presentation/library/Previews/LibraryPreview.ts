import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../business/jobs/ChildRegistry'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { LibraryViewModel } from '../ViewModels/LibraryViewModel'
import { libraryPreviewRules, sampleImage, sampleInputs, withLibraryFolders } from './LibraryWorlds'
import type { LibraryPreset } from './LibraryWorlds'

/** How a preview's place is set up: the preset's world, its mode, what the person does once it has read, and what is done once the clock has started -- each act, then the ticks it plays. */
export interface LibraryPreviewOptions {
  readonly preset?: LibraryPreset
  readonly apogee?: ApogeeMode
  readonly then?: (library: LibraryViewModel) => void | Promise<void>
  readonly script?: ReadonlyArray<readonly [(library: LibraryViewModel) => void | Promise<void>, number]>
}

/** Lets what the clock played land: each awaited read and each replayed job's `for await` takes a turn. */
async function settle(): Promise<void> {
  for (let round = 0; round < 6; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/**
 * The Library place for a preview (design 1n), the Mac app's `LibraryPreview`
 * over its documents: the stand-in answering the place's recordings and the
 * data directory's two files, a still clock, the place read, the old page's
 * samples handed in, the person's act, then the clock's ticks. A place's
 * preview may not import `app`, so it names the mode rules' recording itself
 * (1d).
 */
export function useLibraryPreview(options: LibraryPreviewOptions = {}): LibraryViewModel {
  const [model] = useState(() => {
    const apogee = options.apogee ?? 'installed'
    const child = new StandInChild({ rules: libraryPreviewRules(options.preset ?? 'library'), apogee, folders: withLibraryFolders() }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ still: true })
    const library = new LibraryViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()), child.folders, clock)
    library.useSamples(sampleInputs, sampleImage)
    library.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { library, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.library.appeared()
      await options.then?.(model.library)
      for (const [act, ticks] of options.script ?? []) {
        await settle()
        void act(model.library)
        for (let tick = 0; tick < ticks; tick += 1) {
          model.clock.advance(1)
          await settle()
        }
      }
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.library
}
