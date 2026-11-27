import { describe, expect, test } from 'vitest'
import { libraryPlace, settle } from './LibraryHarness'

async function servers(options: Parameters<typeof libraryPlace>[0] = { preset: 'library:mcp' }) {
  const place = libraryPlace(options)
  await place.library.appeared()
  place.library.pick('mcp')
  return place
}

describe('the MCP servers (design 1n): the Mac app’s MCPViewModel over mcp-list.json and mcp-presets.json, its MCPRemoveTests', () => {
  test('code, connected with its two tools, its command as the CLI runs it; the presets Apogee ships, a refused one in its own words', async () => {
    const { library } = await servers()
    const face = library.face()
    expect(face.rows.map((row) => [row.name, row.meta, row.current, row.pickable])).toEqual([['code', 'connected · 2 tools', true, false]])
    expect(face.mcp).toEqual({
      shown: true,
      name: 'code',
      toggle: 'Disable',
      command: '/opt/homebrew/bin/python3 /Users/someone/.apogee/training/scripts/code_exec_server.py',
      facts: 'connected · 2 tools: run_python, write_input · environment: none',
      presetsNote: '',
      presets: [
        { name: 'code-exec', text: "Runs Python for any model, local or cloud: a working directory kept for the session and deleted after it, the network off, each run bounded -- Apogee's code-execution server", refused: false },
        { name: 'self', text: "'self' is Apogee's own tools, served for another MCP host: registered into Apogee it would serve Apogee the tools it already has natively. Give another host its entry instead (apogee mcp presets)", refused: true },
      ],
    })
    expect(face.newAgent, 'New agent… is the agents tab’s alone').toBe(false)
  })

  test('Disable stays as the prototype draws it: its toast, word for word, and nothing run', async () => {
    const { library, child, said } = await servers()
    const calls = child.calls.length
    library.mcp.toggle()
    expect(said).toEqual(['apogee mcp disable code (prototype)'])
    expect(child.calls.length).toBe(calls)
  })

  test('Remove… asks first; a dismissed question runs nothing; on yes it is config delete-mcp-server code, read back, the CLI’s words said, the server gone from the list', async () => {
    const { library, child, said, asked } = await servers()
    library.mcp.askToRemove()
    expect(asked).toEqual(['mcpRemove'])
    expect(library.confirmWords('mcpRemove')).toEqual({ title: 'Remove the MCP server code?', body: 'apogee config delete-mcp-server code -- its entry leaves the config, so no session connects to it; its files are left alone.', cancel: 'Cancel', alternative: '', confirm: 'Remove its entry', destructive: true })
    const calls = child.calls.length
    library.dismissPending('mcpRemove')
    expect([library.confirmWords('mcpRemove'), child.calls.length]).toEqual([undefined, calls])
    library.mcp.askToRemove()
    await library.confirm('mcpRemove', false)
    await settle()
    expect(child.calls.slice(calls).map((call) => `${call.kind} ${call.arguments.join(' ')}`)).toEqual(['commands config delete-mcp-server code', 'reads mcp list', 'reads mcp presets'])
    expect(said).toEqual(["removed MCP server 'code' from /Users/someone/.apogee/config/config.json"])
    const face = library.face()
    expect([face.rows, face.mcp.shown, face.mcp.presets.length], 'the stand-in keeps no state, so the removal the CLI reported is kept beside its read').toEqual([[], false, 2])
  })

  test('no apogee: no servers and no presets, each said where it is drawn; a config typo: the CLI’s refusal for the list, and the stand-in’s for the presets', async () => {
    const missing = await servers({ preset: 'library:mcp', apogee: 'missing' })
    expect([missing.library.face().note, missing.library.face().mcp.shown, missing.library.face().mcp.presetsNote, missing.library.face().emptyDetail]).toEqual([
      "Apogee isn't installed, so there are no MCP servers to list.",
      false,
      "Apogee isn't installed, so there are no presets to list.",
      '',
    ])
    const typo = await servers({ preset: 'library:mcp', apogee: 'config-typo' })
    expect(typo.library.face().note).toBe("The MCP servers could not be read: apogee mcp: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
  })
})
