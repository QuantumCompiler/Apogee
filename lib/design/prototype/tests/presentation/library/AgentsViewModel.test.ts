import { describe, expect, test } from 'vitest'
import { editorFiles, libraryFolders } from '../../../source/presentation/library/Previews/LibraryWorlds'
import { libraryPlace, settle } from './LibraryHarness'

describe('the agents (design 1n): the Mac app’s AgentsViewModel over agents-list.json, the old page’s words', () => {
  test('the CLI’s eight agents in its order, triage current by its name and its page drawn; the rows do not pick', async () => {
    const { library } = libraryPlace()
    await library.appeared()
    const face = library.face()
    expect(face.rows.map((row) => `${row.name} -- ${row.meta}`)).toEqual([
      'security-review -- shipped · read-only',
      'release-notes -- shipped · read-only',
      'merge-request -- shipped · read-only',
      'code-review -- shipped · read-only',
      'test-gaps -- shipped · read-only',
      'doc-drift -- shipped · read-only',
      'meeting-minutes -- shipped · read-only',
      'triage -- yours · read-only',
    ])
    expect(face.rows.filter((row) => row.current).map((row) => row.name)).toEqual(['triage'])
    expect(face.rows.every((row) => !row.pickable)).toBe(true)
    library.pickRow('security-review')
    expect(library.face().agent.name, 'an agent row does not pick: the old page stays on triage').toBe('triage')
    const agent = library.face().agent
    expect([agent.shown, agent.canDelete, agent.description, agent.facts, agent.runs]).toEqual([true, true, 'Sort incoming issues by area', 'Model: the default · tools: read-only', 'apogee analyze --agent triage runs it.'])
    expect(agent.files).toEqual(['prompts/triage.txt', 'schemas/triage-output.json'])
    expect(face.newAgent).toBe(true)
  })

  test('Edit on the prompt reads `config path` for the data directory and opens the prompt the binary scaffolded in the editor sheet', async () => {
    const { library, child, sheets } = libraryPlace()
    await library.appeared()
    library.agents.edit('prompts/triage.txt')
    expect(child.calls.filter((call) => call.kind === 'commands').map((call) => call.arguments.join(' '))).toEqual(['config path'])
    const editor = library.editorFace()
    expect([editor.shown, editor.title, editor.path, editor.text]).toEqual([true, 'triage · prompts/triage.txt', `${libraryFolders.prompts}/triage.txt`, editorFiles.triage])
    expect(sheets).toEqual(['editor'])
  })

  test('New agent…, Delete… and the schema’s Edit stay as the prototype draws them: each says its toast, word for word, and runs nothing', async () => {
    const { library, child, said, sheets } = libraryPlace()
    await library.appeared()
    const calls = child.calls.length
    library.agents.newAgent()
    library.agents.askToDelete()
    library.agents.edit('schemas/triage-output.json')
    expect(said).toEqual(['apogee agents create <name> (prototype)', 'apogee agents delete triage --purge | --keep-files: confirmed first (prototype)', 'Edited in the app, saved to schemas/triage-output.json (prototype)'])
    expect(child.calls.length).toBe(calls)
    expect(sheets).toEqual([])
  })

  test('no apogee: no agents, the Mac’s words where the list is, and the page asks for an agent; a config typo: the CLI’s refusal, word for word', async () => {
    const missing = libraryPlace({ apogee: 'missing' })
    await missing.library.appeared()
    expect([missing.library.face().rows, missing.library.face().note, missing.library.face().emptyDetail]).toEqual([[], "Apogee isn't installed, so there are no agents to list.", 'Choose an agent.'])
    const typo = libraryPlace({ apogee: 'config-typo' })
    await typo.library.appeared()
    expect(typo.library.face().note).toBe("The agents could not be read: apogee agents: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
    expect(typo.library.face().agent.shown).toBe(false)
    typo.library.agents.edit('prompts/triage.txt')
    await settle()
    expect(typo.library.editorFace().shown, 'no data directory read, so no file to open').toBe(false)
  })
})
