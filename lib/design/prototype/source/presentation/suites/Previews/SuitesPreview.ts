import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../infrastructure/process/StandInChild'
import { SuitesViewModel } from '../ViewModels/SuitesViewModel'
import { suitesPlaceRules } from './SuitesWorlds'

/** How a preview's place is set up: its mode, its answers (the place's world unless named), whether a session could move, and what the person does once it has read. */
export interface SuitesPreviewOptions {
  readonly apogee?: ApogeeMode
  readonly rules?: Rules
  readonly switchable?: boolean
  readonly then?: (suites: SuitesViewModel) => void | Promise<void>
}

/**
 * The Suites place for a preview (design 1p), the Mac app's `SuitesPreview`
 * over its documents: the stand-in answering the place's recordings, read,
 * then the person's act. It names the mode rules' recording itself, as a
 * place's preview may not import `app` (1d).
 */
export function useSuitesPreview(options: SuitesPreviewOptions = {}): SuitesViewModel {
  const [model] = useState(() => {
    const child = new StandInChild({ rules: options.rules ?? suitesPlaceRules(), apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const suites = new SuitesViewModel(new ReadRunner(child))
    suites.sessionSwitchable = () => options.switchable ?? true
    suites.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { suites, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.suites.appeared()
      await options.then?.(model.suites)
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.suites
}
