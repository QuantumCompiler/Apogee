import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../business/jobs/ChildRegistry'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../infrastructure/process/StandInChild'
import type { Folders } from '../../../infrastructure/process/StandInFolders'
import { MediaPreferences } from '../../styles/Media/MediaPreferences'
import { Playback } from '../../styles/Media/Playback'
import { ComposeViewModel } from '../ViewModels/ComposeViewModel'
import { composeFiles, composeWorld, moviesFolder, passedOver, recordedBrief, tidesFiles } from './ComposeWorlds'

/** How a preview's compositions are set up: their answers, the folder, the mode, what the person does once they have read, how far in a run stands, and what is done once the clock has played. */
export interface ComposePreviewOptions {
  readonly rules?: Rules
  readonly folders?: Folders
  readonly apogee?: ApogeeMode
  /** What the person does once the place has read, before the clock starts. */
  readonly then?: (compose: ComposeViewModel) => void | Promise<void>
  /** Start ticks: how far in a run stands. */
  readonly ticks?: number
  /** What the person does once the clock has started and what it played has landed: each act, then how many ticks it plays before the next. */
  readonly script?: ReadonlyArray<readonly [(compose: ComposeViewModel) => void | Promise<void>, number]>
}

/** Lets what the clock played land: each replayed job's `for await` takes a turn. */
async function settle(): Promise<void> {
  for (let round = 0; round < 4; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** `~/Movies/Apogee` as the compositions' previews read it: the old four and the recorded Tides, made. */
export function composeFolders(): Folders {
  return { [moviesFolder]: [...composeFiles(), ...tidesFiles()] }
}

/**
 * The compositions for a preview (design 1l), the Mac app's
 * `ComposePreview` over its documents: the stand-in answering the place's
 * recordings and its folder's projects, a still clock, the place read, the
 * person's act, then the clock started. A place's preview may not import
 * `app`, so it names the mode rules' recording itself (1d).
 */
export function useComposePreview(options: ComposePreviewOptions = {}): ComposeViewModel {
  const [model] = useState(() => {
    const apogee = options.apogee ?? 'installed'
    const child = new StandInChild({ rules: options.rules ?? composeWorld(), apogee, folders: options.folders ?? composeFolders() }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ start: options.ticks ?? 0, still: true })
    const compose = new ComposeViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()), child.folders, clock, new Playback(() => clock.ticks), new MediaPreferences('af_heart'))
    compose.folders = () => [moviesFolder]
    compose.passedOver = passedOver
    compose.startingBrief = recordedBrief
    compose.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { compose, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.compose.appeared()
      await options.then?.(model.compose)
      model.clock.start()
      for (const [act, ticks] of options.script ?? []) {
        await settle()
        await act(model.compose)
        for (let tick = 0; tick < ticks; tick += 1) {
          model.clock.advance(1)
          await settle()
        }
      }
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.compose
}
