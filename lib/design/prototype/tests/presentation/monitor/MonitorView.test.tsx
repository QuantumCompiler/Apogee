import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import suitesSystem from '@recorded/business/reads/Fixtures/suites-system.json?raw'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { Answer } from '../../../source/infrastructure/process/StandInChild'
import { MonitorViewModel } from '../../../source/presentation/monitor/ViewModels/MonitorViewModel'
import { MonitorView } from '../../../source/presentation/monitor/Views/MonitorView'

/** Lets every read the clock started land. */
const settled = () => new Promise((resolve) => setTimeout(resolve, 0))

/** The monitor's markup after `ticks` ticks over a stand-in whose `system` read answers `answers`, one a call. */
async function drawn(answers: readonly Answer[], ticks = 0): Promise<string> {
  const child = new StandInChild({ rules: { reads: [{ arguments: ['system'], answer: answers as [Answer, ...Answer[]] }] }, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
  const clock = new Clock({ still: true })
  const model = new MonitorViewModel(new ReadRunner(child), clock)
  const found = child.locateBinary()
  model.connect(found.case === 'found' ? found.binary : undefined)
  model.opened()
  await settled()
  clock.advance(ticks)
  await settled()
  return renderToStaticMarkup(<MonitorView model={model} close={() => {}} />)
}

/** The opening tags of every element that holds `text` in `markup`, outermost first. */
function ancestorsOf(markup: string, text: string): string[] {
  const at = markup.indexOf(text)
  if (at < 0) throw new Error(`the markup does not say ${text}`)
  const open: string[] = []
  for (const tag of markup.slice(0, at).matchAll(/<(\/?)([a-zA-Z][\w-]*)[^>]*?(\/?)>/g)) {
    if (tag[1] === '/') open.pop()
    else if (tag[3] !== '/' && !['br', 'img', 'input', 'hr'].includes(tag[2] ?? '')) open.push(tag[0])
  }
  return open
}

describe('the monitor view draws what the old panel draws (design 1e)', () => {
  test('the stale line is drawn at full opacity: only the old GPU history bars faded, never its words', async () => {
    const markup = await drawn([{ stdout: suitesSystem }, { timedOut: true }], 5)
    const holders = ancestorsOf(markup, 'reading failed · last value, 2 s old')
    expect(holders.length).toBeGreaterThan(1)
    expect(holders.filter((tag) => /opacity:/.test(tag))).toEqual([])
  })

  test("no model held by the reading process is said in the CLI's words, never as none loaded", async () => {
    const markup = await drawn([{ stdout: suitesSystem }])
    expect(markup).toContain('None by this process -- a session holds its own. A model loads inside the session that needs it')
    expect(markup).not.toContain('No models loaded')
  })
})
