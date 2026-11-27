import configGetDefaultSuite from '../../../../fixtures/business/reads/config-get-default-suite-handmade.txt?raw'
import suitesConfigGetCoding from '../../../../fixtures/business/reads/suites-config-get-coding-handmade.txt?raw'
import { libraryRules } from './LibraryRules'
import type { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'
import { ExecuteDoorSheet } from '../Views/ExecuteDoor'
import { useLibraryPreview } from './LibraryPreview'

/** The execute door's content on its own (design 1g): under the default suite, filled as `execute` fills it, and bare. */
function Door({ model }: { readonly model: SessionLibraryViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={{ width: 560, padding: 20, background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' }}>
      <ExecuteDoorSheet model={model} />
    </div>
  )
}

function rules(defaultSuite: string) {
  const library = libraryRules()
  return {
    reads: library.reads,
    commands: [
      ...library.commands,
      { arguments: ['config', 'get', 'suites'], answer: { stdout: suitesConfigGetCoding } },
      { arguments: ['config', 'get', 'models.default_suite'], answer: { stdout: defaultSuite } },
    ],
  }
}

/** `execute`: under coding, in ~/code/ingest, orchestrating; the line as it will run. */
export const UnderCoding = () => <Door model={useLibraryPreview({ rules: rules(configGetDefaultSuite), unopened: true, then: (library) => library.openExecuteDoor({ suite: 'coding', folder: '~/code/ingest', orchestrate: true }) })} />

/** A fresh door: the default suite, the home folder, not orchestrating. */
export const Fresh = () => <Door model={useLibraryPreview({ rules: rules(configGetDefaultSuite), unopened: true, then: (library) => library.openExecuteDoor() })} />

/** No suite named and none the default: bare on one model, Orchestrate off and its note. */
export const Bare = () => <Door model={useLibraryPreview({ rules: rules('\n'), unopened: true, then: (library) => library.openExecuteDoor() })} />
