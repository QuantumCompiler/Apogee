import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../business/jobs/ChildRegistry'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { ReviewViewModel } from '../ViewModels/ReviewViewModel'
import { reviewForm, reviewPlaceRules, sweepsFolder, withReviewFolders } from './ReviewWorlds'

/** How a preview's place is set up: its mode, what the person does once it has read, and what is done once the clock has started -- each act, then the ticks it plays. */
export interface ReviewPreviewOptions {
  readonly apogee?: ApogeeMode
  readonly then?: (review: ReviewViewModel) => void | Promise<void>
  readonly script?: ReadonlyArray<readonly [(review: ReviewViewModel) => void | Promise<void>, number]>
}

async function settle(): Promise<void> {
  for (let round = 0; round < 6; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/**
 * The Review place for a preview (design 1n), the Mac app's `ReviewPreview`
 * over its documents: the stand-in answering the place's recordings and the
 * CLI's sweeps folder, a still clock, the old tab's form, the place read,
 * the person's act, then the clock's ticks. It names the mode rules'
 * recording itself, as a place's preview may not import `app` (1d).
 */
export function useReviewPreview(options: ReviewPreviewOptions = {}): ReviewViewModel {
  const [model] = useState(() => {
    const apogee = options.apogee ?? 'installed'
    const child = new StandInChild({ rules: reviewPlaceRules(), apogee, folders: withReviewFolders() }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ still: true })
    const review = new ReviewViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()), child.folders)
    review.sweeps = () => [sweepsFolder]
    review.setForm(reviewForm)
    review.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { review, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    model.setUp ??= (async () => {
      await model.review.appeared()
      await options.then?.(model.review)
      for (const [act, ticks] of options.script ?? []) {
        await settle()
        void act(model.review)
        for (let tick = 0; tick < ticks; tick += 1) {
          model.clock.advance(1)
          await settle()
        }
      }
    })()
  }, [model, options])
  return model.review
}
