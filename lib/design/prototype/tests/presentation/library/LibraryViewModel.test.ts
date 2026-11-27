import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import mcpListBadConfig from '@recorded/business/reads/Fixtures/mcp-list-bad-config.err?raw'
import symphoniesListBadConfig from '@recorded/business/reads/Fixtures/symphonies-list-bad-config.err?raw'
import { describe, expect, test } from 'vitest'
import { libraryPlace } from './LibraryHarness'

describe('the Library place (design 1n): its three tabs, as the old header draws them', () => {
  test('Agents, Symphonies and MCP servers switch the list and the page; each tab reads once the place appears', async () => {
    const { library, child } = libraryPlace()
    await library.appeared()
    expect(child.calls.filter((call) => call.kind === 'reads').map((call) => call.arguments.join(' '))).toEqual(['agents list', 'symphonies list', 'mcp list', 'mcp presets'])
    expect(library.face().tabs).toEqual([
      { tab: 'agents', label: 'Agents', current: true },
      { tab: 'symphonies', label: 'Symphonies', current: false },
      { tab: 'mcp', label: 'MCP servers', current: false },
    ])
    library.pick('symphonies')
    expect([library.face().tabs[1]?.current, library.face().rows[0]?.name, library.face().agent.shown, library.face().symphony.shown]).toEqual([true, 'critique-revise', false, true])
    library.pick('mcp')
    expect([library.face().rows[0]?.name, library.face().symphony.shown, library.face().mcp.shown]).toEqual(['code', false, true])
    library.pick('agents')
    expect(library.face().agent.name).toBe('triage')
  })

  test('the config-typo notes are the recordings’ words: the stand-in’s refusal is each recorded .err, byte for byte', async () => {
    const { child } = libraryPlace({ apogee: 'config-typo' })
    const refused = (args: readonly string[]) => (child.run('reads', args) as { run: { stderrTail: { text: string } } }).run.stderrTail.text
    expect(refused(['agents', 'list'])).toBe(agentsListBadConfig)
    expect(refused(['symphonies', 'list'])).toBe(symphoniesListBadConfig)
    expect(refused(['mcp', 'list'])).toBe(mcpListBadConfig)
  })
})
