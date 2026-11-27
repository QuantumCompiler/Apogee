import checkCaptionsSkipped from '@recorded/business/reads/Fixtures/check-captions-skipped.json?raw'
import configGetServerTools from '@recorded/business/reads/Fixtures/config-get-server-tools.txt?raw'
import configGetServerToolsNone from '@recorded/business/reads/Fixtures/config-get-server-tools-none.txt?raw'
import allowedHosts from '@recorded/business/reads/Fixtures/allowed-hosts.txt?raw'
import allowedHostsEmpty from '@recorded/business/reads/Fixtures/allowed-hosts-empty.txt?raw'
import providersAuthList from '@recorded/business/reads/Fixtures/providers-auth-list.json?raw'
import providersRegisterCodex from '@recorded/business/reads/Fixtures/providers-register-codex.json?raw'
import providersScan from '@recorded/business/reads/Fixtures/providers-scan.json?raw'
import xcodeAcpRegistration from '@recorded/business/reads/Fixtures/xcode-acp-registration.json?raw'
import xcodeCheck from '@recorded/business/reads/Fixtures/xcode-check.json?raw'
import xcodeConfigGet from '@recorded/business/reads/Fixtures/xcode-config-get.txt?raw'
import xcodeConfigGetNone from '@recorded/business/reads/Fixtures/xcode-config-get-none.err?raw'
import xcodeMcpPresets from '@recorded/business/reads/Fixtures/xcode-mcp-presets.json?raw'
import { describe, expect, test } from 'vitest'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { CheckReport } from '../../../source/business/reads/Check'
import { AllowedHosts, Permissions, ServerTools } from '../../../source/business/reads/ConfigVerbs'
import { MCPPreset } from '../../../source/business/reads/Library'
import { KeyedProviders, Provider, ProviderRegistration } from '../../../source/business/reads/Providers'
import { AgentRegistration, MCPServerEntry, XcodeCheck } from '../../../source/business/reads/XcodeSetup'
import { CommandOutcome } from '../../../source/business/reads/CommandRunning'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'

const document = (text: string) => {
  const read = ReadDocument.from(text)
  if (!read) throw new Error('not a document')
  return read
}

describe("check's document, as Settings reads it (design 1i, the Mac's CheckRow)", () => {
  test('its rows in order, and its counts', () => {
    const report = CheckReport.of(document(checkCaptionsSkipped))
    expect(report.rows[0]).toEqual({ section: 'Version', name: 'apogee', status: 'ok', detail: '0.2.0', remedy: undefined })
    expect(report.failures).toBe(0)
    expect(report.warnings).toBe(2)
    expect(report.summary).toBe('0 failing, 2 to look at')
  })

  test("Health's rows: what fails, then what to look at, then each skipped row with a remedy -- a passing row, and a skipped one with nothing to do, not drawn", () => {
    const health = CheckReport.of(document(checkCaptionsSkipped)).health
    expect(health.map((row) => `${row.status} ${row.section}/${row.name}`)).toEqual([
      'warn Training/trainer',
      'warn Training/convert set',
      'skipped MCP/code-exec',
      'skipped MLX/runtime',
      'skipped MLX/vision',
      'skipped MLX/embeddings',
      'skipped MLX/video',
      'skipped MLX/voice',
      'skipped Video/runtime',
      'skipped Video/voice runtime',
      'skipped Video/ffmpeg captions',
    ])
  })

  test('the install it describes: the version, the channel and the data folder, in its words', () => {
    expect(CheckReport.of(document(checkCaptionsSkipped)).install).toEqual({ version: '0.2.0', channel: 'release', root: '/Users/someone/.apogee' })
    expect(CheckReport.of(document(xcodeCheck)).install).toEqual({ version: '0.2.0', channel: 'dev', root: '/Users/someone/.apogee-dev' })
  })

  test('nothing to look at says so: "Everything checks out", the Mac\'s words', () => {
    expect(CheckReport.of(document('{"rows":[],"ok":true,"failures":0,"warnings":0}')).summary).toBe('Everything checks out')
  })
})

describe('the config verbs Settings reads', () => {
  test('config get permissions: one `tool: level` a line, in its order', () => {
    expect(Permissions.levels('run_command: ask\nwrite_file: allow\n')).toEqual([
      { tool: 'run_command', level: 'ask' },
      { tool: 'write_file', level: 'allow' },
    ])
    expect(Permissions.levels('\n')).toEqual([])
  })

  test("a tool the config does not list is ask, the CLI's own rule", () => {
    const levels = Permissions.levels('write_file: allow\n')
    expect(Permissions.level(levels, 'write_file')).toBe('allow')
    expect(Permissions.level(levels, 'fetch_url')).toBe('ask')
  })

  test('config get tools.allowed_hosts: one host a line, none for an empty answer', () => {
    expect(AllowedHosts.list(allowedHosts)).toEqual(['docs.python.org', 'developer.apple.com'])
    expect(AllowedHosts.list(allowedHostsEmpty)).toEqual([])
    expect(AllowedHosts.key).toBe('tools.allowed_hosts')
  })

  test('config get backends.<name>.server_tools: held only by the three API types, one tool a line', () => {
    expect(ServerTools.held('anthropic')).toBe(true)
    expect(ServerTools.held('claude-cli')).toBe(false)
    expect(ServerTools.key('claude')).toBe('backends.claude.server_tools')
    expect(ServerTools.list(configGetServerTools)).toEqual(['web_search', 'code_execution'])
    expect(ServerTools.list(configGetServerToolsNone)).toEqual([])
  })

  test('a config value: an empty line is unset, as config get prints an unset key', () => {
    expect(Permissions.value(CommandOutcome.done('\n'))).toBeUndefined()
    expect(Permissions.value(CommandOutcome.done('http://searx.home.arpa:8080\n'))).toBe('http://searx.home.arpa:8080')
    expect(Permissions.value(CommandOutcome.couldNotStart('no recorded answer for apogee config get x'))).toBeUndefined()
  })
})

describe('the providers, as the scan and auth say them', () => {
  test('providers scan: each with its label, tier, backend and type; one it would register says not registered', () => {
    const providers = Provider.list(document(providersScan))
    expect(providers.map((provider) => [provider.provider, provider.label, provider.tier, provider.backend])).toEqual([
      ['claude', 'Claude CLI', 'installed', 'not registered'],
      ['codex', 'Codex CLI', 'installed', 'not registered'],
      ['gemini', 'Gemini CLI', 'installed', 'not registered'],
      ['ollama', 'Ollama CLI', 'installed', 'not registered'],
      ['anthropic', 'Anthropic API', 'not found', ''],
      ['openai', 'OpenAI API', 'not found', ''],
      ['google', 'Google API', 'not found', ''],
    ])
    expect(providers.map((provider) => Provider.registers(provider))).toEqual([true, true, true, true, false, false, false])
    expect(providers.map((provider) => Provider.needsModel(provider))).toEqual([false, false, false, true, false, false, false])
  })

  test('what Register runs: providers register <id>, the TUI\'s r; with a model typed for a type that needs one, config add-backend', () => {
    const [claude, , , ollama] = Provider.list(document(providersScan))
    expect(claude && Provider.registration(claude)).toEqual(['providers', 'register', 'claude'])
    expect(ollama && Provider.registration(ollama, ' llama3.2 ')).toEqual(['config', 'add-backend', 'ollama', '--type', 'ollama-cli', '--model', 'llama3.2'])
  })

  test("providers register's document: its registration lines, then its rosters'", () => {
    expect(ProviderRegistration.of(document(providersRegisterCodex))?.lines[0]).toMatch(/^registered codex/)
    expect(ProviderRegistration.of(document('{"object":"list","data":[]}'))).toBeUndefined()
  })

  test('auth list: the providers with a key stored, names only -- never a key', () => {
    expect(KeyedProviders.of(document(providersAuthList))).toEqual([])
    expect(KeyedProviders.of(document('{"backends":[],"data":[{"provider":"anthropic","stored_at":"2026-11-14T09:00:00Z"}],"object":"list"}'))).toEqual(['anthropic'])
  })
})

describe('Apogee and Xcode (the Mac\'s XcodeSetup)', () => {
  test("acp registration: the fields Xcode's sheet takes, as printed, and the line above them", () => {
    const fields = AgentRegistration.of(document(xcodeAcpRegistration))
    expect(fields).toMatchObject({ name: 'Apogee (dev)', executable: '/Users/someone/.local/bin/apogee-dev', interpreter: undefined, arguments: ['acp'], xcodeMinimum: '26.6' })
    expect(fields?.environment).toEqual([
      { name: 'APOGEE_HOME', value: '/Users/someone/.apogee-dev' },
      { name: 'PATH', value: '/Users/someone/.local/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin' },
    ])
    expect(fields && AgentRegistration.heading(fields)).toBe('Add Apogee (dev) to Xcode 26.6 or later: Settings › Intelligence › Agents › Add an Agent')
    expect(AgentRegistration.of(document('{"object":"list"}'))).toBeUndefined()
  })

  test("mcp presets: the xcode row, its server named by its own register line", () => {
    const xcode = MCPPreset.list(document(xcodeMcpPresets)).find((preset) => preset.name === 'xcode')
    expect(xcode).toMatchObject({ runnable: true, register: 'apogee mcp create xcode --from xcode' })
    expect(xcode && MCPPreset.serverName(xcode)).toBe('xcode')
    expect(xcode && MCPPreset.registration(xcode)).toEqual(['mcp', 'create', 'xcode', '--from', 'xcode'])
    expect(xcode?.description).toMatch(/^Xcode's own tools for any model/)
  })

  test("config get mcp_servers.<name>: its command, or the CLI's no such config key", () => {
    expect(MCPServerEntry.of(CommandOutcome.done(xcodeConfigGet))).toEqual({ case: 'registered', command: '/usr/bin/xcrun' })
    const refused = new BoundedTail()
    refused.append(xcodeConfigGetNone)
    expect(MCPServerEntry.of(CommandOutcome.failed(ChildExit.exited(1), refused))).toEqual({ case: 'notRegistered' })
    expect(MCPServerEntry.of(CommandOutcome.couldNotStart('apogee is not installed'))).toEqual({ case: 'unreadable', reason: 'apogee is not installed' })
  })

  test("check's Xcode section, by its rows' names", () => {
    const rows = CheckReport.of(document(xcodeCheck)).rows.filter((row) => row.section === XcodeCheck.section)
    expect(rows.map((row) => row.name)).toEqual([XcodeCheck.xcode, XcodeCheck.bridge, XcodeCheck.agents, XcodeCheck.approval])
  })
})
