import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import type { ModeRules } from '../../../infrastructure/process/StandInChild'

/**
 * The shared mode rules (design 1d): what every preset's stand-in answers
 * with in the `config-typo` mode -- the CLI's refusal of a config it cannot
 * read, recorded as `agents-list-bad-config.err`, its verb swapped per call
 * as the old prototype's `cfgRefusal` swaps it. Imported here, in a
 * `Previews/` file, since the stand-in is handed its recordings.
 */
export const modeRules: ModeRules = { configTypoRefusal: agentsListBadConfig }
