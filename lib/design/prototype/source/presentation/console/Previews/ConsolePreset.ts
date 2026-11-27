import configScan from '@recorded/business/reads/Fixtures/config-scan.txt?raw'
import completeModels from '../../../../fixtures/business/reads/complete-models-handmade.txt?raw'
import modelsListText from '../../../../fixtures/business/reads/models-list-handmade.txt?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'

/**
 * What the console is answered with on every preset (design 1g), beside the
 * preset's own rules: `apogee models list` as text -- hand-made in the CLI's
 * table (`models.cpp`), the `console` preset's old frame's lines --
 * `apogee config scan`, the CLI's recording the Mac's console previews read,
 * and `apogee __complete models`, the ten words the old frame offers. Any
 * other line runs as a job that prints nothing and exits 0, so the console
 * says the old prototype's "Done." (the document's default). `app` hands
 * these to the console's own stand-in child (`Preset.console`), so the
 * catch-all answers only a line typed there, never a place's job.
 */
export function consoleRules(): Required<Pick<Rules, 'jobs' | 'commands'>> {
  return {
    jobs: [
      { arguments: [], answer: {} },
      { arguments: ['models', 'list'], answer: { stdout: modelsListText } },
      { arguments: ['config', 'scan'], answer: { stdout: configScan } },
    ],
    commands: [{ arguments: ['__complete', 'models'], answer: { stdout: completeModels } }],
  }
}
