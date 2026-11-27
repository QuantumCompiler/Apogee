import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import modelsListServerTools from '@recorded/business/reads/Fixtures/models-list-server-tools.json?raw'
import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { ApogeeClient } from '../../../source/business/session/ApogeeClient'
import { ChatLaunch } from '../../../source/business/session/ChatLaunch'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import { sessionSamples } from '../../../source/presentation/session/Previews/SessionPresets'
import { libraryRules } from '../../../source/presentation/sessions/Previews/LibraryRules'
import { SessionLibraryViewModel } from '../../../source/presentation/sessions/ViewModels/SessionLibraryViewModel'
import { SessionList } from '../../../source/presentation/sessions/Views/SessionList'

describe('the session list, drawn: what only the view decides (design 1g)', () => {
  test("a row whose backend's kind the config does not type keeps the glyph's place, so every title starts in line", async () => {
    // chat:pr's recorded config (models-list-server-tools.json) types no local backend: root's rows have no kind.
    const rules = libraryRules()
    const child = new StandInChild({ rules: { ...rules, reads: [...rules.reads, { arguments: ['models', 'list'], answer: { stdout: modelsListServerTools } }] }, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ still: true })
    const library = new SessionLibraryViewModel(new ApogeeClient(child, clock), new ReadRunner(child), new JobStarter(child, clock), clock, sessionSamples)
    await library.start()
    const markup = renderToStaticMarkup(<SessionList model={library} />)
    const row = (title: string) => markup.slice(markup.indexOf(`title="${title}`) - 1).split('</button>')[0] ?? ''
    expect(row('TaskGroup timeouts in the ingest runner')).toContain('data-glyph="none"')
    expect(row('Review PR 482: retry budget for uploads')).toContain('aria-label="subscription"')
    expect(row('Review PR 482: retry budget for uploads')).not.toContain('data-glyph="none"')
  })

  test('the private row is drawn selected only while Sessions is shown, as the old `privateBg` is; its text stays primary', async () => {
    const opening = '{"model":"claude","next_turn":1,"private":true,"protocol_version":1,"type":"session"}'
    const rules = libraryRules()
    const child = new StandInChild({ rules: { ...rules, chats: [{ arguments: ChatLaunch.arguments({ privately: true }), answer: { stdout: opening } }] }, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ still: true })
    const library = new SessionLibraryViewModel(new ApogeeClient(child, clock), new ReadRunner(child), new JobStarter(child, clock), clock, sessionSamples)
    await library.start()
    await library.replace({ privately: true })
    const privateRow = () => {
      const markup = renderToStaticMarkup(<SessionList model={library} />)
      return markup.slice(markup.lastIndexOf('<button', markup.indexOf('Private session'))).split('</button>')[0] ?? ''
    }
    expect(privateRow()).toContain('background:var(--surface-selection)')
    library.showing(false)
    expect(privateRow()).not.toContain('background:var(--surface-selection)')
    expect(privateRow()).toContain('color:var(--text-primary)')
  })
})
