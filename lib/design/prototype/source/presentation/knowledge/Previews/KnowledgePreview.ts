import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../infrastructure/process/StandInChild'
import { CollectionsViewModel } from '../ViewModels/CollectionsViewModel'
import { RecordsViewModel } from '../ViewModels/RecordsViewModel'
import { collectionQuestion, knowledgePlaceRules, recordedSearches, recordsQuestion } from './KnowledgeWorlds'

/** The place's two view-models, as a preview holds them. */
export interface KnowledgePlace {
  readonly records: RecordsViewModel
  readonly collections: CollectionsViewModel
}

/** How a preview's place is set up: its mode, its answers (the place's world unless named), what the person does once it has read, and how many ticks a run it starts has played -- a still frame's (GUI M22). */
export interface KnowledgePreviewOptions {
  readonly apogee?: ApogeeMode
  readonly rules?: Rules
  readonly then?: (place: KnowledgePlace) => void | Promise<void>
  readonly ticks?: number
}

/**
 * The Knowledge place for a preview (design 1o), the Mac app's
 * `KnowledgePreview` over its documents: the stand-in answering the place's
 * recordings, both view-models read, then the person's act. It names the
 * mode rules' recording itself, as a place's preview may not import `app`
 * (1d). Nothing here plays on the clock but a run the person starts, which
 * stands `ticks` in, as a still preset's does (GUI M22).
 */
export function useKnowledgePreview(options: KnowledgePreviewOptions = {}): KnowledgePlace {
  const [model] = useState(() => {
    const child = new StandInChild({ rules: options.rules ?? knowledgePlaceRules(), apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const reads = new ReadRunner(child)
    const clock = new Clock({ still: true, start: options.ticks ?? 0 })
    clock.start()
    const jobs = new JobStarter(child, clock)
    const place: KnowledgePlace = { records: new RecordsViewModel(reads, jobs), collections: new CollectionsViewModel(reads, jobs) }
    place.collections.recordedSearches = recordedSearches
    place.records.emptyQuestion = recordsQuestion
    place.collections.emptyQuestions = { notes: collectionQuestion }
    const binary = child.locateBinary().case === 'found' ? StandInChild.binary : undefined
    place.records.connect(binary)
    place.collections.connect(binary)
    return { place, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.place.records.appeared()
      await model.place.collections.appeared()
      await options.then?.(model.place)
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.place
}
