import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'
import mcpList from '@recorded/business/reads/Fixtures/mcp-list.json?raw'
import symphoniesList from '@recorded/business/reads/Fixtures/symphonies-list.json?raw'
import symphonyPlay from '@recorded/business/reads/Fixtures/symphony-play.json?raw'
import symphonyShow from '@recorded/business/reads/Fixtures/symphony-show.json?raw'
import { describe, expect, test } from 'vitest'
import { AgentEntry, MCPServer } from '../../../source/business/reads/Library'
import { Symphony, SymphonyPlay } from '../../../source/business/reads/Symphonies'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const documentOf = (text: string) => ReadDocument.from(text) as ReadDocument

describe("the Library's reads (design 1n): the Mac app's AgentEntry, MCPServer, Symphony and SymphonyPlay (Library.swift)", () => {
  test('agents list: every agent in the CLI’s order, its files relative to the data directory, shipped or the person’s own', () => {
    const agents = AgentEntry.list(documentOf(agentsList))
    expect(agents.map((agent) => agent.name)).toEqual(['security-review', 'release-notes', 'merge-request', 'code-review', 'test-gaps', 'doc-drift', 'meeting-minutes', 'triage'])
    const triage = agents.at(-1)
    expect(triage).toEqual({ name: 'triage', description: 'Sort incoming issues by area', bundled: false, model: '', tools: 'read-only', prompts: ['prompts/triage.txt'], schemas: ['schemas/triage-output.json'], collection: '' })
    expect(agents[0]?.bundled).toBe(true)
    expect(AgentEntry.list(documentOf('{"object":"list","data":[{"description":"no name"}]}'))).toEqual([])
  })

  test('mcp list: each server as the CLI connects to it, its environment said only as set or not', () => {
    const [code] = MCPServer.list(documentOf(mcpList))
    expect(code).toEqual({ name: 'code', command: '/opt/homebrew/bin/python3', args: ['/Users/someone/.apogee/training/scripts/code_exec_server.py'], enabled: true, envSet: false, state: 'connected', tools: ['run_python', 'write_input'], error: undefined })
    // A server whose row leaves `enabled` out is enabled, as the Swift reads it.
    expect(MCPServer.list(documentOf('{"object":"list","data":[{"name":"x"}]}'))[0]?.enabled).toBe(true)
  })

  test('symphonies list: each symphony whole -- its source, its path, its input, its stages in play order, a stage that plays another -- and show reads one', () => {
    const symphonies = Symphony.list(documentOf(symphoniesList))
    const digest = symphonies.find((symphony) => symphony.name === 'digest')
    expect(digest?.stages.map((stage) => [stage.name, stage.role, stage.plays])).toEqual([
      ['summary', undefined, 'summarize-verify'],
      ['card', 'extraction', undefined],
    ])
    const describeAnswer = symphonies.find((symphony) => symphony.name === 'describe-answer')
    expect(describeAnswer?.takesImage).toBe(true)
    expect(describeAnswer?.inputDescription).toBe('A question about the image, which is given with --image.')
    const quick = symphonies.find((symphony) => symphony.name === 'quick-check')
    expect([quick?.source, quick?.path, Symphony.isOwnFile(quick as Symphony)]).toEqual(['config', undefined, false])
    const shown = Symphony.read(documentOf(symphonyShow).value)
    expect([shown?.name, shown?.source, shown?.path, shown?.problems, Symphony.isOwnFile(shown as Symphony)]).toEqual(['notes-digest', 'file', '/Users/someone/.apogee/symphonies/notes-digest.yaml', [], true])
  })

  test('symphonies play: each stage’s answer and the backend that gave it, and the output; another document is none', () => {
    const play = SymphonyPlay.read(documentOf(symphonyPlay))
    expect(play?.symphony).toBe('critique-revise')
    expect(play?.stages.map((stage) => `${stage.name} · ${stage.role} (${stage.backend})`)).toEqual(['critique · utility (writer)', 'revise · chat (writer)'])
    expect(play?.stages[0]?.answer).toBe('1. The draft never says who it is for.\n2. The second paragraph repeats the first.')
    expect(play?.output).toBe('A tighter draft, for new contributors: what the tool does, then how to run it.')
    expect(SymphonyPlay.read(documentOf(symphonyShow))).toBeUndefined()
  })
})
