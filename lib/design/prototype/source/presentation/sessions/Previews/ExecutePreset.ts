import configGetDefaultSuite from '../../../../fixtures/business/reads/config-get-default-suite-handmade.txt?raw'
import suitesConfigGetCoding from '../../../../fixtures/business/reads/suites-config-get-coding-handmade.txt?raw'
import type { Preset } from '../../../infrastructure/process/Preset'
import { chatPlayPreset } from '../../session/Previews/SessionPresets'
import type { PresetOptions } from '../../session/Previews/SessionPresets'
import { withLibraryRules } from './LibraryRules'

/**
 * `execute` (design 1g): the execute door over the `play` session -- the old
 * `applyStart('execute')`'s `chatId: 'play'` with `overlay: 'execute'` -- on
 * `chat:play`'s capture and reads. No recorded suite list names `coding`
 * and no `config get models.default_suite` answer is recorded, so the door's
 * two reads are hand-made: `suites-config-get-coding-handmade.txt`, the
 * recorded suites with the capture's `coding` among them, and
 * `config-get-default-suite-handmade.txt`, `coding`, which the door's "The
 * default suite (coding)" reads.
 */
export function executePreset(options: PresetOptions): Preset {
  const play = chatPlayPreset(options)
  const rules = withLibraryRules(play.rules)
  return {
    ...play,
    rules: {
      ...rules,
      commands: [
        ...(rules.commands ?? []),
        { arguments: ['config', 'get', 'suites'], answer: { stdout: suitesConfigGetCoding } },
        { arguments: ['config', 'get', 'models.default_suite'], answer: { stdout: configGetDefaultSuite } },
      ],
    },
    route: 'execute',
  }
}
