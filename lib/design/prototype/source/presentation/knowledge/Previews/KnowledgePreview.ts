import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ReadRunner } from '../../../business/reads/ReadRunner'
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

/** How a preview's place is set up: its mode, its answers (the place's world unless named), and what the person does once it has read. */
export interface KnowledgePreviewOptions {
  readonly apogee?: ApogeeMode
  readonly rules?: Rules
  readonly then?: (place: KnowledgePlace) => void | Promise<void>
}

/**
 * The Knowledge place for a preview (design 1o), the Mac app's
 * `KnowledgePreview` over its documents: the stand-in answering the place's
 * recordings, both view-models read, then the person's act. It names the
 * mode rules' recording itself, as a place's preview may not import `app`
 * (1d). Nothing here plays on the clock.
 */
export function useKnowledgePreview(options: KnowledgePreviewOptions = {}): KnowledgePlace {
  const [model] = useState(() => {
    const child = new StandInChild({ rules: options.rules ?? knowledgePlaceRules(), apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const reads = new ReadRunner(child)
    const place: KnowledgePlace = { records: new RecordsViewModel(reads), collections: new CollectionsViewModel(reads) }
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
