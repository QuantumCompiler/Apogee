import checkCaptionsSkipped from '@recorded/business/reads/Fixtures/check-captions-skipped.json?raw'
import xcodeAcpRegistrationOlder from '@recorded/business/reads/Fixtures/xcode-acp-registration-older.err?raw'
import xcodeCheck from '@recorded/business/reads/Fixtures/xcode-check.json?raw'
import xcodeCheckAbsent from '@recorded/business/reads/Fixtures/xcode-check-absent.json?raw'
import xcodeConfigGet from '@recorded/business/reads/Fixtures/xcode-config-get.txt?raw'
import { describe, expect, test } from 'vitest'
import { CheckReport } from '../../../source/business/reads/Check'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import type { Rules } from '../../../source/infrastructure/process/StandInChild'
import { settingsWorld } from '../../../source/presentation/settings/Previews/SettingsWorlds'
import { XcodeSettings } from '../../../source/presentation/settings/ViewModels/XcodeSettings'
import { frozenComponent } from '../styles/Frozen'
import { settled, standIn } from './SettingsHarness'

const rowsOf = (text: string) => CheckReport.of(ReadDocument.from(text) ?? (() => { throw new Error('no document') })()).rows

/** The pane over `rules`, read, with `check`'s rows handed in as Settings hands them. */
async function pane(rules: Rules = settingsWorld('xcode'), check: string = xcodeCheck) {
  const stand = standIn(rules)
  const model = new XcodeSettings(stand.reads)
  const said: string[] = []
  const asked: string[] = []
  model.say = (text) => said.push(text)
  model.ask = (kind) => asked.push(kind)
  model.checkRows = () => rowsOf(check)
  model.use(stand.binary)
  await model.read()
  return { ...stand, model, said, asked }
}

/** The old pane's values at settings:xcode. */
function oldPane() {
  const old = frozenComponent({ start: 'settings:xcode', still: true }) as unknown as { renderVals(): { set: Record<string, unknown> } }
  return old.renderVals().set as { xcodeRows: Array<{ text: string; mark: string }>; xcodeDesc: string; xcodeState: string; xcodeNote: string; xcodeHeading: string; xcodeApproval: string; xcodeFields: Array<{ label: string; value: string; canCopy: boolean; aria: string }> }
}

describe('Settings › Xcode, over GUI 5a\'s recordings (design 1i, the Mac\'s XcodeSettings)', () => {
  test("its face is the old pane's, every line from the recordings: check's four rows, the preset's words, the registration's fields", async () => {
    const { model, child } = await pane()
    const face = model.face()
    const old = oldPane()
    expect(face.rows.map((row) => [row.mark, row.text])).toEqual(old.xcodeRows.map((row) => [row.mark, row.text]))
    expect(face.tools).toMatchObject({ description: old.xcodeDesc, state: old.xcodeState, note: old.xcodeNote, canRegister: true, command: 'apogee mcp create xcode --from xcode', canRemove: false })
    expect(face.agent.heading).toBe(old.xcodeHeading)
    expect(face.agent.approval).toBe(old.xcodeApproval)
    expect(face.agent.fields.map((field) => [field.label, field.value, field.canCopy, field.aria])).toEqual(old.xcodeFields.map((field) => [field.label, field.value, field.canCopy, field.aria]))
    expect(child.unanswered).toEqual([])
    expect(child.calls.map((call) => call.arguments.join(' '))).toEqual(['mcp presets', 'config get mcp_servers.xcode', 'acp registration'])
  })

  test('Register writes the config and connects to nothing: mcp create, its words kept and said, the pane and check read back', async () => {
    const { model, child, said } = await pane()
    let rereads = 0
    model.rereadCheck = async () => {
      rereads += 1
    }
    await model.register()
    expect(child.calls.filter((call) => call.kind === 'commands').map((call) => call.arguments.join(' '))).toContain('mcp create xcode --from xcode')
    expect(said).toEqual(["registered MCP server 'xcode'"])
    expect(model.face().said).toMatchObject({ shown: true, title: "Registered for Apogee's agents" })
    expect(model.face().said.text.split('\n')).toHaveLength(7)
    expect(rereads).toBe(1)
    model.dismissSaid()
    expect(model.face().said.shown).toBe(false)
  })

  test("registered (config get names its command): Remove… asks first in the old words, and yes removes the entry alone", async () => {
    const world = settingsWorld('xcode')
    const rules: Rules = { ...world, commands: [...world.commands, { arguments: ['config', 'get', 'mcp_servers.xcode'], answer: { stdout: xcodeConfigGet } }] }
    const { model, child, asked, said } = await pane(rules)
    expect(model.face().tools).toMatchObject({ state: "In Apogee's config as xcode: /usr/bin/xcrun", canRegister: false, canRemove: true })
    model.askToRemove()
    expect(asked).toEqual(['xcodeRemove'])
    expect(model.confirmWords).toEqual({ title: "Remove Xcode's tools from Apogee?", body: 'apogee config delete-mcp-server xcode -- its config entry goes; Xcode is left as it is.', confirm: 'Remove' })
    await model.remove()
    expect(child.calls.filter((call) => call.arguments[1] === 'delete-mcp-server').map((call) => call.arguments)).toEqual([['config', 'delete-mcp-server', 'xcode']])
    expect(said).toEqual(["removed MCP server 'xcode' from /Users/someone/.apogee-dev/config/config.json"])
  })

  test('a binary whose check says nothing of Xcode says so; one with Xcode absent offers neither route', async () => {
    const none = await pane(settingsWorld('default'), checkCaptionsSkipped)
    expect(none.model.face().rows).toEqual([])
    expect(none.model.face().rowsLacking).toBe('This apogee checks nothing of Xcode.')
    const absent = await pane(settingsWorld('xcode'), xcodeCheckAbsent)
    expect(absent.model.face().tools.canRegister).toBe(false)
    expect(absent.model.face().agent).toMatchObject({ fields: [], notOffered: true })
    expect(absent.model.face().rows.some((row) => row.mark === '–')).toBe(true)
  })

  test("a binary before the CLI's 56d refuses acp registration: its own words, no fields", async () => {
    const world = settingsWorld('xcode')
    const rules: Rules = { ...world, reads: [...world.reads, { arguments: ['acp', 'registration'], answer: { stderr: xcodeAcpRegistrationOlder, exit: 109 } }] }
    const { model } = await pane(rules)
    expect(model.face().agent.fields).toEqual([])
    expect(model.face().agent.lacking).toMatch(/^This apogee printed no registration: /)
  })

  test('nothing is read before a binary is found, and check not yet read says so', async () => {
    const stand = standIn('xcode')
    const model = new XcodeSettings(stand.reads)
    await model.read()
    await settled()
    expect(stand.child.calls).toEqual([])
    expect(model.face().rowsLacking).toBe('Not read yet.')
  })

  test("a field's Copy says so in the toast, as the old pane does", async () => {
    const { model, said } = await pane()
    model.copied()
    expect(said).toEqual(['Copied to the clipboard'])
  })
})
