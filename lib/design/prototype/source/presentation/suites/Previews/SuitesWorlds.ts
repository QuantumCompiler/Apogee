import suitesConfigGet from '@recorded/business/reads/Fixtures/suites-config-get.txt?raw'
import suitesConfigGetCloud from '@recorded/business/reads/Fixtures/suites-config-get-cloud.txt?raw'
import suitesConfigGetFast from '@recorded/business/reads/Fixtures/suites-config-get-fast.txt?raw'
import suitesConfigGetPair from '@recorded/business/reads/Fixtures/suites-config-get-pair.txt?raw'
import suitesConfigGetShared from '@recorded/business/reads/Fixtures/suites-config-get-shared.txt?raw'
import suitesConfigGetVast from '@recorded/business/reads/Fixtures/suites-config-get-vast.txt?raw'
import suitesModelsStatus from '@recorded/business/reads/Fixtures/suites-models-status.json?raw'
import statusCloud from '@recorded/business/reads/Fixtures/suites-models-status-cloud.json?raw'
import statusFast from '@recorded/business/reads/Fixtures/suites-models-status-fast.json?raw'
import statusPair from '@recorded/business/reads/Fixtures/suites-models-status-pair.json?raw'
import statusShared from '@recorded/business/reads/Fixtures/suites-models-status-shared.json?raw'
import statusVast from '@recorded/business/reads/Fixtures/suites-models-status-vast.json?raw'
import configGetSuitesCoding from '../../../../fixtures/business/reads/config-get-suites-coding-handmade.txt?raw'
import setCloud from '../../../../fixtures/business/reads/config-set-default-suite-cloud-handmade.txt?raw'
import setFast from '../../../../fixtures/business/reads/config-set-default-suite-fast-handmade.txt?raw'
import setOff from '../../../../fixtures/business/reads/config-set-default-suite-off-handmade.txt?raw'
import setPair from '../../../../fixtures/business/reads/config-set-default-suite-pair-handmade.txt?raw'
import setShared from '../../../../fixtures/business/reads/config-set-default-suite-shared-handmade.txt?raw'
import setVast from '../../../../fixtures/business/reads/config-set-default-suite-vast-handmade.txt?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import { suitesRules } from './SuitesWorldRules'
import type { Lookup } from './SuitesWorldRules'

export * from './SuitesWorldRules'

/**
 * The Suites place's recordings (design 1p), each a `?raw` import the build
 * inlines, named in `recordings` by its file name; the answers built from
 * them are `SuitesWorldRules.ts`, which imports none. The suites are the
 * five the Mac app recorded (`suites-*`), the same the session's chooser
 * offers; only `config set-default-suite`'s one-line answers and the
 * members of the execute preset's `coding` are hand-made.
 */
const recordings: Readonly<Record<string, string>> = {
  'suites-config-get.txt': suitesConfigGet,
  'suites-config-get-cloud.txt': suitesConfigGetCloud,
  'suites-config-get-fast.txt': suitesConfigGetFast,
  'suites-config-get-pair.txt': suitesConfigGetPair,
  'suites-config-get-shared.txt': suitesConfigGetShared,
  'suites-config-get-vast.txt': suitesConfigGetVast,
  'suites-models-status.json': suitesModelsStatus,
  'suites-models-status-cloud.json': statusCloud,
  'suites-models-status-fast.json': statusFast,
  'suites-models-status-pair.json': statusPair,
  'suites-models-status-shared.json': statusShared,
  'suites-models-status-vast.json': statusVast,
  'config-set-default-suite-cloud-handmade.txt': setCloud,
  'config-set-default-suite-fast-handmade.txt': setFast,
  'config-set-default-suite-pair-handmade.txt': setPair,
  'config-set-default-suite-shared-handmade.txt': setShared,
  'config-set-default-suite-vast-handmade.txt': setVast,
  'config-set-default-suite-off-handmade.txt': setOff,
  'config-get-suites-coding-handmade.txt': configGetSuitesCoding,
}

/** A file's text, by its name; a name no import holds is refused, naming it. */
export function recording(name: string): string {
  const text = recordings[name]
  if (text === undefined) throw new Error(`no fixture named ${name}`)
  return text
}

/** What Suites reads and runs on every preset (`Presets.ts`' `modelling`): ⌘3 opens it from every screen. */
export function suitesPlaceRules(look: Lookup = recording): Required<Pick<Rules, 'reads' | 'commands'>> {
  return suitesRules(look)
}
