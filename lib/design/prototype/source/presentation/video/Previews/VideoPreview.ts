import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../business/jobs/ChildRegistry'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../infrastructure/process/StandInChild'
import { StandInFolders } from '../../../infrastructure/process/StandInFolders'
import type { Folders } from '../../../infrastructure/process/StandInFolders'
import { VideoViewModel } from '../ViewModels/VideoViewModel'
import { paperBoat, videoFolders, videoWorld } from './VideoWorlds'

/** How a preview's place is set up: its answers, its folder, its mode, what the person does once it has read, how far in a run stands, and what is done once the clock has played. */
export interface VideoPreviewOptions {
  readonly rules?: Rules
  readonly folders?: Folders
  readonly apogee?: ApogeeMode
  /** What the person does once the place has read, before the clock starts. */
  readonly then?: (video: VideoViewModel) => void | Promise<void>
  /** Start ticks: how far in a run or an install stands. */
  readonly ticks?: number
  /** What the person does once the clock has started and what it played has landed: each act, then how many ticks it plays before the next. */
  readonly script?: ReadonlyArray<readonly [(video: VideoViewModel) => void | Promise<void>, number]>
}

/** Lets what the clock played land: each replayed job's `for await` takes a turn. */
async function settle(): Promise<void> {
  for (let round = 0; round < 4; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/**
 * The Video place for a preview (design 1k), the Mac app's `PreviewSupport`
 * over its documents: the stand-in answering the video world's recordings
 * and its folder's records, a still clock, the place read, the new-clip form
 * holding the old frame's paper boat, the person's act, then the clock
 * started. A place's preview may not import `app`, so it names the mode
 * rules' recording itself (1d).
 */
export function useVideoPreview(options: VideoPreviewOptions = {}): VideoViewModel {
  const [model] = useState(() => {
    const apogee = options.apogee ?? 'installed'
    const child = new StandInChild({ rules: options.rules ?? videoWorld(), apogee }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ start: options.ticks ?? 0, still: true })
    const registry = new ChildRegistry()
    const video = new VideoViewModel(new ReadRunner(child), new JobStarter(child, clock, registry), new StandInFolders({ folders: options.folders ?? videoFolders(), apogee }), clock)
    video.registry = registry
    video.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { video, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.video.appeared()
      model.video.edit({ prompt: paperBoat })
      await options.then?.(model.video)
      model.clock.start()
      for (const [act, ticks] of options.script ?? []) {
        await settle()
        await act(model.video)
        for (let tick = 0; tick < ticks; tick += 1) {
          model.clock.advance(1)
          await settle()
        }
      }
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.video
}
