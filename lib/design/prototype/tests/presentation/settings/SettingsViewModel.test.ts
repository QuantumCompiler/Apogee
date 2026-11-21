import { describe, expect, test } from 'vitest'
import authListAnthropic from '../../../fixtures/business/reads/auth-list-anthropic-handmade.json?raw'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { fixLines, settingsPreferences, settingsWorld } from '../../../source/presentation/settings/Previews/SettingsWorlds'
import type { SettingsWorld } from '../../../source/presentation/settings/Previews/SettingsWorlds'
import { SettingsViewModel, settingsPanes } from '../../../source/presentation/settings/ViewModels/SettingsViewModel'
import type { SettingsPane, SettingsPreferences } from '../../../source/presentation/settings/ViewModels/SettingsViewModel'
import { chord, KeyMap } from '../../../source/presentation/shell/ViewModels/KeyMap'
import type { BoundKey } from '../../../source/presentation/shell/ViewModels/KeyMap'
import { SessionKeys } from '../../../source/presentation/sessions/ViewModels/SessionKeys'
import { ThemeService } from '../../../source/presentation/styles/Appearance/ThemeService'
import { frozenComponent } from '../styles/Frozen'
import { settled, standIn } from './SettingsHarness'

/** The keys the window binds, as the composition hands them: the shell's and Sessions'. */
const bound: readonly BoundKey[] = [...KeyMap.shell, ...SessionKeys.bindings]

/** The Settings window over a world, connected and read, its doors recorded. */
async function settings(world: SettingsWorld | Rules = 'default', apogee: ApogeeMode = 'installed', keys: readonly BoundKey[] = bound, pane: SettingsPane = 'general', preferences: SettingsPreferences = settingsPreferences) {
  const stand = standIn(world, { still: true }, apogee)
  const model = new SettingsViewModel(stand.reads, stand.jobs, new ThemeService(), () => keys, preferences)
  const said: string[] = []
  const presented: string[] = []
  const asked: string[] = []
  const library: string[] = []
  let closedSheets = 0
  model.say = (text) => said.push(text)
  model.present = (kind) => presented.push(kind)
  model.ask = (kind) => asked.push(kind)
  model.dismissSheet = () => (closedSheets += 1)
  model.openLibrary = () => library.push('backends')
  model.connect(stand.child.locateBinary().case === 'found' ? stand.binary : undefined, settingsPreferences.passedOver)
  model.pick(pane)
  await model.appeared()
  await settled()
  return { ...stand, model, said, presented, asked, library, closedSheets: () => closedSheets }
}

/** The old pane's values, as the frozen prototype builds them at `start`. */
function oldSet(start: string): Record<string, unknown> {
  const old = frozenComponent({ start, still: true }) as unknown as { renderVals(): { set: Record<string, unknown> } }
  return old.renderVals().set
}

/** Every string in `value`, however deep, never into `skipped` -- the services it was handed, the CLI's side: what a planted key is searched for in. */
function strings(value: unknown, skipped: readonly unknown[] = [], seen = new Set<unknown>(skipped)): string[] {
  if (typeof value === 'string') return [value]
  if (value === null || typeof value !== 'object' || seen.has(value)) return []
  seen.add(value)
  return Object.getOwnPropertyNames(value).flatMap((key) => strings((value as Record<string, unknown>)[key], skipped, seen))
}

describe('the Settings window: its panes (design 1i, the Mac’s SettingsViewModel, the old prototype’s ten)', () => {
  test("the ten panes in the old order and words, the one shown current and titled; every one opens", async () => {
    const { model } = await settings()
    const old = oldSet('settings:general') as { panes: Array<{ label: string; current: boolean }>; title: string }
    expect(model.face().panes.map((pane) => [pane.label, pane.current])).toEqual(old.panes.map((pane) => [pane.label, pane.current]))
    expect(model.face().title).toBe(old.title)
    expect(settingsPanes.map(([id]) => id)).toEqual(['general', 'appearance', 'roles', 'media', 'backends', 'permissions', 'web', 'memory', 'keyboard', 'xcode'])
    for (const [id, label] of settingsPanes) {
      model.pick(id)
      expect(model.face().pane).toBe(id)
      expect(model.face().title).toBe(label)
    }
  })

  test("every read answered from a recording or a hand-made file: nothing left unanswered, in the default world", async () => {
    const { model, child } = await settings()
    model.pick('xcode')
    await settled()
    expect(child.unanswered).toEqual([])
    expect(child.calls.map((call) => `${call.kind}: ${call.arguments.join(' ')}`)).toEqual(
      expect.arrayContaining(['reads: models list', 'reads: auth list', 'reads: providers scan', 'reads: check', 'commands: config get permissions', 'commands: config get tools.search.url', 'commands: config get memory.recall', 'commands: config get tools.allowed_hosts', 'reads: chats recall', 'commands: config get models.default_video', 'reads: models status', 'reads: mcp presets', 'reads: acp registration']),
    )
  })

  test("the window's foot: the install check describes, in its words", async () => {
    const { model } = await settings()
    expect(model.face().foot).toEqual(['apogee 0.2.0 · release', 'data in /Users/someone/.apogee'])
  })

  test("missing: nothing runs, and the window says so in the Mac app's words", async () => {
    const { model, child } = await settings('default', 'missing')
    expect(child.calls).toEqual([])
    expect(model.face().failure).toBe('Apogee isn’t installed, so there are no settings to read.')
    expect(model.face().foot).toEqual([])
  })

  test("config-typo: check refused, in the CLI's words, the red line under the title", async () => {
    const { model } = await settings('default', 'config-typo')
    expect(model.face().failure).toMatch(/^apogee check: /)
  })

  test('Done closes the window: the key being typed and the failure line go with it, as the old close does', async () => {
    const { model } = await settings('default', 'config-typo')
    model.editKey('anthropic')
    model.typeKey('sk-ant-typed')
    model.closed()
    expect(model.face().failure).toBe('')
    expect(model.face().backends.keys.every((row) => !row.editing)).toBe(true)
    expect(strings(model.face())).not.toContain('sk-ant-typed')
  })
})

describe('General', () => {
  test("model for new sessions: the chat role's own choice, as models status names it, among the backends that chat; a pick runs config set-default", async () => {
    const { model, said, child } = await settings()
    const general = model.face().general
    expect(general.model.value).toBe('plain')
    expect(general.model.options.map((option) => option.value)).toEqual(['claude', 'claude-app', 'fasthelper', 'fastroot', 'helper', 'plain', 'root', 'scribe', 'vast'])
    await model.setDefaultModel('root')
    expect(child.calls.some((call) => call.arguments.join(' ') === 'config set-default root')).toBe(true)
    expect(said).toEqual(['models.default = root'])
  })

  test("the app's own choices, as the old pane draws them: thinking, effort, new sessions and opening", async () => {
    const { model } = await settings()
    const general = model.face().general
    expect(general.thinking.map((choice) => [choice.label, choice.current])).toEqual([['Off', false], ['On', false], ['Auto', true]])
    expect(general.effort.value).toBe('default')
    expect(general.effort.options.map((option) => option.label)).toEqual(['the model’s default', 'low', 'medium', 'high', 'xhigh', 'max'])
    expect(general.newSessions.map((choice) => [choice.label, choice.current])).toEqual([['Saved', true], ['Private', false]])
    expect(general.onLaunch.map((choice) => [choice.label, choice.current])).toEqual([['Start a new session', false], ['Reopen the last session', true]])
    model.setThinking('off')
    model.setNewSessions('private')
    model.setOnLaunch('new')
    model.setEffort('high')
    const changed = model.face().general
    expect([changed.thinking.find((choice) => choice.current)?.value, changed.newSessions.find((choice) => choice.current)?.value, changed.onLaunch.find((choice) => choice.current)?.value, changed.effort.value]).toEqual(['off', 'private', 'new', 'high'])
  })

  test('the command-line tool: the binary found, its version and channel, and the override discovery passed over, in its words', async () => {
    const { model, said } = await settings()
    const cli = model.face().general.cli
    expect(cli.lines).toEqual(['/Users/someone/.local/bin/apogee · 0.2.0 · release channel', 'data in /Users/someone/.apogee'])
    expect(cli.passedOver).toBe(oldSet('settings:general').binSkipped)
    expect(cli.useInstalled).toBe(true)
    expect(cli.next).toBe('')
    model.useInstalled()
    expect(said).toEqual(['The app looks for apogee where it is installed, from its next launch'])
    expect(model.face().general.cli.useInstalled).toBe(false)
  })
})

describe('Video & voice', () => {
  test('the video pipelines and MLX video models the list names, the config\'s default chosen, and None; a pick runs config set-default-video', async () => {
    // The settings world's list names one: the MLX video set config get models.default_video names.
    const { model, said, child } = await settings()
    const media = model.face().media
    expect(media.video.value).toBe('Wan2.2-TI2V-5B/mlx/5e6f708192a3')
    expect(media.video.options.map((option) => option.value)).toEqual(['Wan2.2-TI2V-5B/mlx/5e6f708192a3', ''])
    expect(media.video.options.at(-1)?.label).toBe('None: each clip names its pipeline')
    await model.setDefaultVideo('')
    expect(child.calls.some((call) => call.arguments.join(' ') === 'config set-default-video off')).toBe(true)
    expect(said).toEqual(['models.default_video = (none)'])
  })

  test("the voice models listed, the voices their models info names, the captions note and the runtimes, each in check's words", async () => {
    const { model } = await settings()
    const media = model.face().media
    expect(media.voiceModel).toBe('Kokoro-82M/mlx/6f708192a3b4, Kokoro-82M/voice/5f3a2c91d0e4')
    expect(media.voices.map((option) => option.label)).toEqual(['af_heart · default', 'am_adam', 'bf_emma'])
    expect(media.voice).toBe('af_heart')
    expect(media.captions).toBe(true)
    expect(media.captionsNote).toBe('no subtitles filter (this ffmpeg was built without libass) -- a composition keeps its captions beside the video as an SRT')
    expect(media.saveTo).toEqual(['video · ~/Movies/Apogee', 'speech · ~/Music/Apogee'])
    expect(media.runtimes.map((line) => [line.name, line.status])).toEqual([
      ['Video · runtime', 'not set up'],
      ['Video · voice runtime', 'not set up'],
      ['Video · ffmpeg captions', 'no subtitles filter (this ffmpeg was built without libass)'],
      ['Video · ffmpeg h264', 'libx264'],
    ])
    model.toggleCaptions()
    expect(model.face().media.captions).toBe(false)
  })
})

describe('Backends & keys', () => {
  test('the old sections over the CLI: the local backends, the Python environment\'s runtimes, the API keys, the subscriptions', async () => {
    const { model } = await settings()
    const backends = model.face().backends
    expect(backends.sections.map((section) => [section.title, section.rows.map((row) => row.name)])).toEqual([
      ['ON THIS MAC', ['fasthelper', 'fastroot', 'helper', 'plain', 'root', 'scribe', 'vast']],
      ['VIDEO AND VOICE · IN THE SAME PYTHON ENVIRONMENT', ['Video · runtime', 'Video · voice runtime', 'Video · ffmpeg captions', 'Video · ffmpeg h264']],
    ])
    expect(backends.subscriptions.map((section) => [section.title, section.rows.map((row) => [row.name, row.status])])).toEqual([['SUBSCRIPTIONS · THROUGH THE VENDOR’S APP', [['claude-app', 'installed']]]])
    expect(backends.sections[0]?.rows.find((row) => row.name === 'root')).toMatchObject({ status: 'ok', detail: 'llamacpp · base-Q4_K_M.gguf' })
    // A runtime's check row: its state the status word, the reason after ` -- ` the line under it, its remedy under that.
    expect(backends.sections[1]?.rows[0]).toMatchObject({ status: 'not set up', detail: 'torch is not installed in the Python environment at /Users/someone/.apogee/training/venv', consent: ['apogee train setup --with video'] })
    expect(backends.sections[1]?.rows[3]).toMatchObject({ status: 'libx264', detail: 'clips and compositions encode as H.264', consent: [] })
    expect(backends.keys.map((row) => [row.name, row.status, row.actions.map((action) => action.label)])).toEqual([
      ['Anthropic API', 'not set', ['Add key…']],
      ['OpenAI API', 'not set', ['Add key…']],
      ['Google API', 'not set', ['Add key…']],
    ])
    expect(backends.keys[0]?.consent).toEqual(['Vendor-run tools consented: web_search, code_execution -- the vendor runs them under your key'])
    expect(backends.keys[0]?.detail).toBe('claude · claude-sonnet-4-5')
  })

  test('with a key stored (auth list names it), Replace key… and Remove; Remove runs auth clear', async () => {
    const world = settingsWorld('default')
    const rules: Rules = { ...world, reads: [...world.reads, { arguments: ['auth', 'list'], answer: { stdout: authListAnthropic } }] }
    const { model, said, child } = await settings(rules)
    expect(model.face().backends.keys[0]).toMatchObject({ status: 'key set', statusColor: 'var(--model-api)' })
    expect(model.face().backends.keys[0]?.actions.map((action) => action.label)).toEqual(['Replace key…', 'Remove'])
    await model.removeKey('anthropic')
    expect(child.calls.some((call) => call.arguments.join(' ') === 'auth clear anthropic')).toBe(true)
    expect(said).toEqual(['cleared the stored key for anthropic'])
  })

  test('a key is never kept: Save key hands it to auth add on its input and clears it, and a planted key is in no field of the view-model', async () => {
    const { model, child, said, reads, jobs, clock, registry } = await settings()
    const planted = 'sk-ant-PLANTED-0123456789'
    model.editKey('anthropic')
    expect(model.face().backends.keys[0]).toMatchObject({ editing: true, placeholder: 'sk-ant-…' })
    model.typeKey(planted)
    expect(model.face().backends.keyEmpty).toBe(false)
    await model.saveKey()
    const sent = child.calls.find((call) => call.arguments.join(' ') === 'auth add anthropic --stdin')
    expect(sent?.input).toBe(`${planted}\n`)
    expect(said).toEqual(['stored a key for anthropic in /Users/someone/.apogee/config/credentials.json'])
    expect(model.face().backends.keys[0]?.editing).toBe(false)
    // The whole view-model, its parts and its closures' owners, but not the stand-in it was handed: the CLI's side, which took the key on its input.
    const cli = [reads, jobs, child, clock, registry]
    expect(strings(model, cli).filter((text) => text.includes('PLANTED'))).toEqual([])
    expect(strings(model, cli).length, 'the search reached the view-model\'s fields').toBeGreaterThan(100)
    expect(strings(model.face()).filter((text) => text.includes('PLANTED'))).toEqual([])
  })

  test("providers on this machine: the scan's rows, Register… on each it would register; the sheet in the TUI's words, providers register its document", async () => {
    const { model, presented, said, child } = await settings()
    const providers = model.face().backends.providers
    expect(providers.map((row) => [row.label, row.state, row.canRegister])).toEqual([
      ['Claude CLI', 'installed · not registered', true],
      ['Codex CLI', 'installed · not registered', true],
      ['Gemini CLI', 'installed · not registered', true],
      ['Ollama CLI', 'installed · not registered', true],
      ['Anthropic API', 'not found', false],
      ['OpenAI API', 'not found', false],
      ['Google API', 'not found', false],
    ])
    model.askToRegister('codex')
    expect(presented).toEqual(['registerProvider'])
    expect(model.face().register).toMatchObject({ title: 'Found codex -- register it as a backend?', subtitle: 'Codex CLI · installed', command: 'apogee providers register codex', needsModel: false, canRegister: true })
    await model.register()
    expect(child.calls.some((call) => call.kind === 'reads' && call.arguments.join(' ') === 'providers register codex')).toBe(true)
    expect(said).toEqual(['registered codex (codex-cli)'])
    expect(model.face().backends.registered).toMatchObject({ shown: true, title: 'Registered codex', text: "registered codex (codex-cli)\nmodels.default set to 'codex' (none was set)\nfetched codex-cli's models: 2 on the roster" })
  })

  test("Ollama's model typed: the sheet's command is the config add-backend the CLI names for it", async () => {
    const { model, said } = await settings()
    model.askToRegister('ollama')
    expect(model.face().register).toMatchObject({ needsModel: true, command: 'apogee providers register ollama' })
    model.typeRegisterModel('llama3.2')
    expect(model.face().register.command).toBe('apogee config add-backend ollama --type ollama-cli --model llama3.2')
    await model.register()
    expect(said).toEqual(["models.default set to 'ollama' (none was set)"])
  })

  test("Health: check's summary and rows -- what fails, what to look at, what is not set up and its remedy -- in its words", async () => {
    const { model } = await settings()
    const health = model.face().backends.health
    expect(health.summary).toBe((oldSet('settings:backends') as { healthSummary: string }).healthSummary)
    expect(health.rows[0]).toEqual({ text: 'Training · trainer: mlx fits this host, but its requirement set is not installed', color: 'var(--status-warning)', remedy: 'apogee train setup --trainer mlx' })
    expect(health.rows.at(-1)?.text).toBe('Not set up · Video · ffmpeg captions: no subtitles filter (this ffmpeg was built without libass) -- a composition keeps its captions beside the video as an SRT')
    expect(health.fixing).toBe(false)
  })

  test('check --fix runs as a job: working, Stop on, both buttons off; it ends finished, Health read again, said in the toast', async () => {
    const { model, clock, said, child } = await settings()
    model.fix(['check', '--fix'])
    expect(model.face().backends.health).toMatchObject({ fixing: true, busy: 'working', canStop: true, command: 'check --fix' })
    const reads = child.calls.filter((call) => call.arguments.join(' ') === 'check').length
    clock.advance(fixLines() + 1)
    await settled()
    expect(model.face().backends.health.fixing).toBe(false)
    expect(said).toEqual(['check --fix finished'])
    expect(child.calls.filter((call) => call.arguments.join(' ') === 'check').length).toBe(reads + 1)
  })

  test('mid-run, the busy line is the last line the run printed', async () => {
    const { model, clock } = await settings()
    model.fix(['check', '--fix'])
    clock.advance(4)
    await settled()
    expect(model.face().backends.health.busy).toBe('  ok   apogee  0.2.0')
  })

  test("fixfailed: check --fix exits 1 while a row still fails -- the failure the red line, failed in the toast, the failing row first in Health", async () => {
    const { model, clock, said } = await settings('fixfailed')
    expect(model.face().backends.health).toMatchObject({ summary: '1 failing, 2 to look at', summaryColor: 'var(--status-error)' })
    expect(model.face().backends.health.rows[0]).toMatchObject({ text: 'MCP · server: github: command not found on PATH: npx', color: 'var(--status-error)', remedy: 'install it, or: apogee config delete-mcp-server github' })
    model.fix(['check', '--fix'])
    clock.advance(fixLines('fixfailed') + 1)
    await settled()
    const old = oldSet('settings:fixfailed') as { failure: string }
    expect(model.face().failure).toBe(old.failure)
    expect(said).toEqual(['check --fix failed: it exited with status 1'])
  })

  test('Stop ends the run, said stopped', async () => {
    const { model, said } = await settings()
    model.fix(['check', '--fix'])
    await model.stopFix()
    await settled()
    expect(said).toEqual(['check --fix stopped'])
    expect(model.face().backends.health.fixing).toBe(false)
  })
})

describe('Permissions', () => {
  test("the old pane's four tools, each its level as config get permissions says it -- unlisted is ask, the CLI's rule -- and a pick runs config set-permission", async () => {
    const { model, said, child } = await settings()
    const old = oldSet('settings:permissions') as { tools: Array<{ name: string; desc: string }> }
    const tools = model.face().permissions.tools
    expect(tools.map((tool) => [tool.name, tool.desc])).toEqual(old.tools.map((tool) => [tool.name, tool.desc]))
    expect(tools.map((tool) => tool.level)).toEqual(['allow', 'ask', 'ask', 'ask'])
    await model.setPermission('fetch_url', 'deny')
    expect(child.calls.some((call) => call.arguments.join(' ') === 'config set-permission fetch_url deny')).toBe(true)
    expect(said).toEqual(['permissions.fetch_url = deny'])
    expect(model.face().permissions).toMatchObject({ always: [], session: [], noAlways: true, noSession: true })
  })

  test("Remove on an always allowance: the entry goes and its tool asks again, as the old pane sets it -- config set-permission <tool> ask", async () => {
    const always = [{ label: 'write_file in ~/code/ingest', from: 'From “Make fetch retries configurable”', tool: 'write_file' }]
    const { model, said, child } = await settings('default', 'installed', bound, 'permissions', { ...settingsPreferences, always })
    await model.removeAlways(0)
    expect(model.face().permissions).toMatchObject({ always: [], noAlways: true })
    expect(child.calls.some((call) => call.arguments.join(' ') === 'config set-permission write_file ask')).toBe(true)
    expect(said).toEqual(['permissions.write_file = ask'])
  })
})

describe('Web access', () => {
  test('the hosts config get tools.allowed_hosts reads; Remove and Add run their verbs, said in the CLI\'s words', async () => {
    const { model, said } = await settings()
    expect(model.face().web.sites).toEqual(['docs.python.org', 'developer.apple.com'])
    await model.removeSite('developer.apple.com')
    model.typeSite('https://docs.python.org/3/')
    expect(model.face().web.siteEmpty).toBe(false)
    await model.addSite()
    expect(said).toEqual(['removed developer.apple.com from tools.allowed_hosts', 'tools.allowed_hosts includes docs.python.org'])
    expect(model.face().web.siteDraft).toBe('')
  })

  test('the web search address set: shown, On; Set off while the field is blank, and a set runs config set', async () => {
    const { model, said, child } = await settings()
    const old = oldSet('settings:web') as { searxShown: string; searxNote: string }
    expect(model.face().web).toMatchObject({ search: old.searxShown, searchNote: old.searxNote, searchColor: 'var(--status-accent)', searchEmpty: true })
    // A blank address is never set: it would clear the address with no word (GUI 6c).
    model.typeSearch('   ')
    await model.setSearch()
    expect(child.calls.some((call) => call.arguments[1] === 'set')).toBe(false)
    model.typeSearch('http://localhost:8888')
    await model.setSearch()
    expect(said).toEqual(['tools.search.url = http://localhost:8888'])
  })

  test('nosearch: unset, as a fresh config prints it, said not set, its note the old one', async () => {
    const { model } = await settings('nosearch')
    const old = oldSet('settings:nosearch') as { searxShown: string; searxNote: string }
    expect(model.face().web).toMatchObject({ search: old.searxShown, searchNote: old.searxNote, searchColor: 'var(--text-tertiary)', searchShownColor: 'var(--text-tertiary)' })
  })
})

describe('Memory', () => {
  test("recall on, as config get memory.recall reads it; what it holds, as chats recall lists it; a toggle runs config set", async () => {
    const { model, said } = await settings()
    expect(model.face().memory).toMatchObject({ recall: true, recallLabel: 'On', holds: '8 sessions summarized · in /Users/someone/.apogee/memory/chats.db', canForget: true })
    await model.toggleRecall()
    expect(said).toEqual(['memory.recall = false'])
  })

  test("Forget all summaries… asks first, in the old words with what recall holds now, then runs chats recall --forget --yes", async () => {
    const { model, asked, said } = await settings()
    await model.askToForget()
    expect(asked).toEqual(['settingsForget'])
    expect(model.confirmWords('settingsForget')).toEqual({
      title: 'Forget what recall holds?',
      body: "Now: recall holds 8 chat summaries in /Users/someone/.apogee/memory/chats.db. apogee chats recall --forget --yes -- every chat's summary goes, so no later session recalls them; every transcript stays.",
      confirm: 'Forget every summary',
    })
    await model.confirm('settingsForget')
    expect(said).toEqual(['forgot 8 chat summaries -- every transcript stays; chats finished from now on are summarised again (apogee config set memory.recall false stops it)'])
  })
})

describe('Keyboard', () => {
  test("every key the window binds, as the shell's map lists it: the design's 23 lines", async () => {
    const { model } = await settings()
    expect(model.face().keyboard).toEqual(KeyMap.reference)
  })

  test('a planted extra key appears, with no edit to settings', async () => {
    const { model } = await settings('default', 'installed', [...bound, { chord: chord('j', 'control', 'command'), label: 'Jump to the latest turn' }])
    expect(model.face().keyboard.at(-1)).toEqual({ label: 'Jump to the latest turn', keys: '⌃⌘J' })
  })
})

describe('Models & roles', () => {
  test('Open models library goes to Models, Backends', async () => {
    const { model, library } = await settings()
    model.showLibrary()
    expect(library).toEqual(['backends'])
  })
})
