import { describe, expect, test } from 'vitest'
import deleteNone from '@recorded/business/reads/Fixtures/delete-none.txt?raw'
import deletePlanNone from '@recorded/business/reads/Fixtures/delete-plan-none.txt?raw'
import { handles, llamaPull, modelsWorld } from '../../../source/presentation/models/Previews/ModelsWorlds'
import type { Rule } from '../../../source/infrastructure/process/StandInChild'
import { ConvertForm } from '../../../source/presentation/models/ViewModels/StoreForms'
import { ModelsViewModel } from '../../../source/presentation/models/ViewModels/ModelsViewModel'
import type { ModelsWorld } from '../../../source/presentation/models/Previews/ModelsWorlds'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'
import { settled, standIn } from './ModelsHarness'

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })

async function place(world: ModelsWorld = 'store', apogee: ApogeeMode = 'installed') {
  const stand = standIn(world, {}, apogee)
  const model = new ModelsViewModel(stand.reads, stand.jobs, stand.clock)
  const said: string[] = []
  const presented: string[] = []
  const asked: string[] = []
  const used: string[] = []
  model.say = (text) => said.push(text)
  model.present = (kind) => presented.push(kind)
  model.ask = (kind) => asked.push(kind)
  model.useInSession = (backend) => used.push(backend)
  model.connect(stand.child.locateBinary().case === 'found' ? stand.binary : undefined)
  await model.appeared()
  await settled()
  return { ...stand, model, said, presented, asked, used }
}

describe('the Models place: its tabs, its Backends and its Roles (the Mac’s ModelsViewModel, the prototype’s screen)', () => {
  test('the stub’s two hooks stand: the store tab the monitor opens, and each time Models comes into view', () => {
    const stand = standIn('store')
    const models = new ModelsViewModel(stand.reads, stand.jobs, stand.clock)
    expect(models.name).toBe('Models')
    expect(models.tab).toBe('backends')
    models.showStore()
    expect(models.tab).toBe('store')
    void models.appeared()
    void models.appeared()
    expect(models.appearances).toBe(2)
  })

  test('the four tabs in the prototype’s order, each with its count; the jobs counted only while they run', async () => {
    const { model, child } = await place()
    expect(model.face().tabs).toEqual([
      { tab: 'backends', label: 'Backends', current: true, count: '13', running: false },
      { tab: 'store', label: 'Stored weights', current: false, count: '10', running: false },
      { tab: 'jobs', label: 'Jobs', current: false, count: '', running: false },
      { tab: 'roles', label: 'Roles', current: false, count: '', running: false },
    ])
    model.pick('roles')
    expect(model.face().tab).toBe('roles')
    expect(child.unanswered).toEqual([])
  })

  test('Backends: the configured backends under the prototype’s two sections, each row the CLI’s words', async () => {
    const { model } = await place()
    const { sections } = model.face().backends
    expect(sections.map((section) => [section.title, section.rows.map((row) => row.name)])).toEqual([
      ['ON THIS MAC', ['Gemma4-12B-4bit', 'Gemma4-E4B-4bit', 'Qwen3-8B-4bit', 'fasthelper', 'fastroot', 'helper', 'llama3.2-1b-Q4KM', 'plain', 'root', 'scribe', 'vast']],
      ['CLOUD AND SUBSCRIPTIONS', ['claude', 'claude-app']],
    ])
    const local = sections[0]?.rows.find((row) => row.name === 'llama3.2-1b-Q4KM')
    expect(local).toMatchObject({ kind: 'local', format: 'GGUF', formatSub: '', size: '770 MiB', window: '32K', memory: '1.2 GiB', status: 'ok', roles: '' })
    const vision = sections[0]?.rows.find((row) => row.name === 'Gemma4-E4B-4bit')
    expect(vision).toMatchObject({ format: 'MLX · vision', formatSub: '4-bit (affine, group 64)', status: 'no environment' })
    const fastroot = sections[0]?.rows.find((row) => row.name === 'fastroot')
    expect(fastroot).toMatchObject({ kind: 'none', format: 'Other', size: '—', roles: 'chat' })
    expect(sections[1]?.rows.map((row) => [row.name, row.kind, row.format, row.memory, row.status])).toEqual([
      ['claude', 'api', 'Anthropic', 'none', 'no key'],
      ['claude-app', 'subscription', 'Claude Code', 'none', 'installed'],
    ])
  })

  test('a local backend’s details: its record under the prototype’s facts, its lineage from the CLI, its files; the actions it offers', async () => {
    const { model } = await place()
    await model.select('llama3.2-1b-Q4KM')
    const detail = model.face().backends.detail
    expect(detail.shown).toBe(true)
    expect(detail.tag).toBe('local · this Mac')
    expect(detail.actions.map((action) => action.label)).toEqual(['Use in the open session', 'Assign a role…', 'Show its weights', 'Remove the backend…', 'Delete with its weights…'])
    expect(detail.facts.map((fact) => fact.label)).toEqual(['Backend', 'Format', 'On disk', 'Window', 'Memory', 'Runs on', 'Path', 'Weights'])
    expect(detail.facts.find((fact) => fact.label === 'Memory')?.value).toBe('weights 770 MiB + cache 544 MiB = 1.2 GiB')
    expect(detail.lineage.map((step) => [step.label, step.how, step.isLink])).toEqual([
      [handles.llamaQ4, 'quantized to Q4_K_M from Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41 (recorded)', false],
      [handles.llamaF16, 'converted from Llama3.2-1B-Instruct/safetensors/edaba290ce7f (recorded)', true],
      [handles.llamaSnapshot, 'pulled from meta-llama/Llama-3.2-1B-Instruct (Hugging Face)', false],
    ])
    expect(detail.files).toEqual([{ path: '/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf', size: '770 MiB' }])
    expect(detail.adapters).toMatchObject({ shown: true, none: true, noneText: 'None. An adapter applies over llama3.2-1b-Q4KM’s weights at load, never merged into them.' })
    expect(detail.drafter.shown).toBe(false)
  })

  test('Use in the open session, Assign a role…, Show its weights: each the prototype’s act', async () => {
    const { model, used } = await place()
    await model.select('llama3.2-1b-Q4KM')
    await model.act('useInSession')
    expect(used).toEqual(['llama3.2-1b-Q4KM'])
    await model.act('assignRole')
    expect(model.tab).toBe('roles')
    await model.act('showWeights')
    expect(model.tab).toBe('store')
    expect(model.store.selected).toBe(handles.llamaQ4)
  })

  test('Delete with its weights… asks with the CLI’s plan, its second door only where it names a backend; Remove the backend… asks, then runs config delete-backend', async () => {
    const { model, asked, said, child } = await place()
    await model.select('llama3.2-1b-Q4KM')
    await model.act('deleteWithWeights')
    expect(asked).toEqual(['model'])
    const words = model.confirmWords('model')
    expect(words).toMatchObject({ title: 'Delete llama3.2-1b-Q4KM?', confirm: 'Delete its files', alternative: 'Delete its files and the backends that use them', destructive: true })
    expect(words?.detail.endsWith("apogee models delete llama3.2-1b-Q4KM --yes -- this can't be undone.")).toBe(true)
    await model.confirm('model', false)
    expect(child.calls.find((call) => call.arguments.includes('--yes'))?.arguments).toEqual(['models', 'delete', 'llama3.2-1b-Q4KM', '--yes'])
    expect(model.face().backends.narration.title).toBe('Deleted llama3.2-1b-Q4KM')
    await model.select('llama3.2-1b-Q4KM')
    await model.act('removeBackend')
    expect(asked.at(-1)).toBe('unregister')
    expect(model.confirmWords('unregister')).toMatchObject({ title: 'Remove the backend llama3.2-1b-Q4KM?', detail: 'apogee config delete-backend llama3.2-1b-Q4KM' })
    await model.confirm('unregister', false)
    expect(said.at(-1)).toBe('Removed the backend llama3.2-1b-Q4KM; its weights stay')
  })

  test('a vision MLX backend with no drafter offers Pair a drafter…: the binary’s own completion, the drafter’s line, then config set-backend --draft', async () => {
    const { model, presented, said } = await place()
    await model.select('Gemma4-E4B-4bit')
    expect(model.face().backends.detail.drafter).toMatchObject({ shown: true, paired: false, canPair: true })
    await model.askToPairDrafter()
    expect(presented).toEqual(['pairDrafter'])
    const sheet = model.face().pairSheet
    expect(sheet.title).toBe('Pair a drafter with Gemma4-E4B-4bit')
    expect(sheet.offers.map((offer) => [offer.handle, offer.current])).toEqual([['Gemma4-E4B-Assistant/mlx/4d5e6f708192', true]])
    expect(sheet.line).toBe('a drafter (Gemma4AssistantForCausalLM) for a 2560-wide gemma4 model, its record naming Gemma4-E4B')
    expect(sheet.command).toBe('apogee config set-backend Gemma4-E4B-4bit --draft /Users/someone/.apogee/models/Gemma4-E4B-Assistant/mlx/4d5e6f708192')
    await model.pairDrafter()
    expect(said).toEqual(['Gemma4-E4B-4bit drafts with Gemma4-E4B-Assistant/mlx/4d5e6f708192'])
  })

  test('a base the binary offers no drafter: the sheet says so, and Pair waits', async () => {
    const { model } = await place()
    await model.select('Gemma4-12B-4bit')
    await model.askToPairDrafter()
    const sheet = model.face().pairSheet
    expect(sheet.offers).toEqual([])
    expect(sheet.warning).toBe('No stored drafter pairs with Gemma4-12B-4bit. A drafter is a draft head converted to MLX for a base of its family and width -- Stored weights converts one with Convert to MLX…, then it is offered here.')
    expect(sheet.canPair).toBe(false)
  })

  test('Edit adapters…: the whole list set in one command, the CLI’s line said', async () => {
    const { model, presented, said } = await place()
    await model.select('llama3.2-1b-Q4KM')
    model.editAdapters()
    expect(presented).toEqual(['adapters'])
    model.editAdaptersForm((form) => form.add())
    expect(model.face().adaptersSheet.command).toBe('apogee config set-backend llama3.2-1b-Q4KM --adapters /Users/someone/Downloads/house-style.lora.gguf')
    await model.applyAdapters()
    expect(said.at(-1)).toBe("edited backend 'llama3.2-1b-Q4KM' in /Users/someone/.apogee/config/config.json: set adapters; adapters pinned to /Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf (sha256 8d51e6c04a27)")
  })

  test('Roles: the six the CLI resolves, under the suite it names, each moved through its verb', async () => {
    const { model, said, child } = await place()
    const roles = model.face().roles
    expect(roles.suiteLine).toBe('Roles resolve under suite fast (Suites changes it)')
    expect(roles.rows.map((row) => [row.label, row.key, row.value, row.note])).toEqual([
      ['Chat', 'chat', 'plain', 'fastroot (via suite fast)'],
      ['Embedding', 'embedding', '', 'plain (via models.default)'],
      ['Extraction', 'extraction', '', 'plain (via models.default)'],
      ['Vision', 'vision', '', "(unset -- the chat's own backend)"],
      ['Transcription', 'transcription', '', "(unset -- the chat's own backend)"],
      ['Utility', 'utility', '', 'fasthelper (via suite fast)'],
    ])
    expect(roles.rows[0]?.options[0], 'Chat keeps the config’s own choice, models.default, as the old picker does: no Not set').toEqual({ value: 'Gemma4-12B-4bit', label: 'Gemma4-12B-4bit' })
    expect(roles.rows[1]?.options[0], 'a role a suite or models.default fills reads Not set').toEqual({ value: '', label: 'Not set' })
    const options = (key: string) => roles.rows.find((row) => row.key === key)?.options.map((option) => option.value) ?? []
    const every = ['Gemma4-12B-4bit', 'Gemma4-E4B-4bit', 'Qwen3-8B-4bit', 'claude', 'claude-app', 'fasthelper', 'fastroot', 'helper', 'llama3.2-1b-Q4KM', 'plain', 'root', 'scribe', 'vast']
    expect(options('chat'), 'the chat role: what chats, as the CLI’s role_refusal takes it').toEqual(every)
    expect(options('chat')).not.toContain('Gemma4-E4B-Assistant/mlx/4d5e6f708192')
    expect(options('embedding'), 'the embedding role leaves out the mlx backends that chat: the CLI refuses them (M33)').toEqual(['', ...every.filter((name) => !['Gemma4-12B-4bit', 'Gemma4-E4B-4bit', 'Qwen3-8B-4bit'].includes(name))])
    expect(options('utility')).toEqual(['', ...every])
    await model.assign('utility', 'helper')
    expect(child.calls.find((call) => call.arguments[1] === 'set-default-utility')?.arguments).toEqual(['config', 'set-default-utility', 'helper'])
    expect(said).toEqual(['Utility: helper'])
  })

  test('Download from Hugging Face… opens the sheet on the prototype’s default; Start download runs the pull and shows Jobs', async () => {
    const { model, presented } = await place('fresh')
    model.openDownload()
    expect(presented).toEqual(['download'])
    expect(model.face().downloadSheet.repo).toBe('Qwen/Qwen3-8B')
    model.editDownload((form) => (form.repo = 'meta-llama/Llama-3.2-1B-Instruct'))
    model.startDownload()
    expect(model.tab).toBe('jobs')
    expect(model.downloads.downloads[0]?.args).toEqual(llamaPull)
  })

  test('with no binary the place reads nothing, and draws the prototype’s empty table (its missing-CLI state is a follow-up)', async () => {
    const { model, child } = await place('store', 'missing')
    expect(child.calls).toEqual([])
    expect(model.face().backends.empty).toBe('Nothing here yet.')
  })

  test('with no binary Start download starts nothing: no card, no count, the sheet’s Start waiting (the Mac’s canStart)', async () => {
    const { model, child } = await place('fresh', 'missing')
    model.openDownload('meta-llama/Llama-3.2-1B-Instruct')
    expect(model.canStartDownload).toBe(false)
    expect(model.face().downloadSheet).toMatchObject({ shown: true, canStart: false })
    model.startDownload()
    expect(model.downloads.downloads).toEqual([])
    expect(model.face().tabs.find((tab) => tab.tab === 'jobs')?.count).toBe('')
    expect(model.face().downloadSheet.shown, 'the sheet stays, its Start greyed').toBe(true)
    model.store.startConvert?.(ConvertForm.gguf(handles.llamaSnapshot, 2482980600))
    expect(model.tab, 'a store job that never started shows no Jobs').toBe('backends')
    expect(child.calls).toEqual([])
  })

  test('a backend’s details say the CLI’s own note on it, in secondary text, as the old pane does: no environment, no key', async () => {
    const { model } = await place()
    await model.select('Gemma4-E4B-4bit')
    expect(model.face().backends.detail).toMatchObject({
      note: 'there is no Python environment at /Users/someone/.apogee/training/venv -- the mlx backend runs mlx-lm there, never the system Python -- apogee train setup --with mlx',
      noteColor: 'var(--text-secondary)',
    })
    await model.select('claude')
    expect(model.face().backends.detail).toMatchObject({ note: "no API key found; run 'apogee auth add anthropic'", noteColor: 'var(--text-secondary)' })
    await model.select('llama3.2-1b-Q4KM')
    expect(model.face().backends.detail.note, 'a row with no note says none').toBe('')
  })

  test('an API backend’s FACTS end with where its data goes, in the CLI’s type word, as the old pane’s Data row', async () => {
    const { model } = await place()
    await model.select('claude')
    const facts = model.face().backends.detail.facts
    expect(facts.map((fact) => fact.label)).toEqual(['Provider', 'Billing', 'Key', 'Window', 'Data'])
    expect(facts.at(-1)).toEqual({ label: 'Data', value: 'sent to the anthropic API', mono: false })
  })

  test('Open in library on a model the list does not name opens Backends with nothing selected, never the backend shown before', async () => {
    const { model } = await place()
    await model.select('llama3.2-1b-Q4KM')
    model.pick('roles')
    await model.openBackend('apogee')
    expect(model.tab).toBe('backends')
    expect(model.selected).toBeUndefined()
    expect(model.selectedRow).toBeUndefined()
    expect(model.face().backends.detail.shown).toBe(false)
  })

  test('Remove the backend… on the one models.default names: the CLI’s cleared line kept on the card (M33, delete-backend-clears.txt’s shape)', async () => {
    const { model, said } = await place()
    await model.select('plain')
    await model.act('removeBackend')
    await model.confirm('unregister', false)
    const narration = model.face().backends.narration
    expect(narration.shown).toBe(true)
    expect(narration.title).toBe('Removed the backend plain; its weights stay')
    expect(narration.text).toBe("removed backend 'plain' from /Users/someone/.apogee/config/config.json\nmodels.default cleared: it named 'plain'")
    expect(said.at(-1)).toBe('Removed the backend plain; its weights stay')
  })

  test('Delete with its weights… on a backend whose plan names no backend offers one door, and its yes runs --yes alone', async () => {
    const world = modelsWorld('store')
    // The CLI's recorded plan that names no backend, answering this backend's delete (the later of two rules as long wins).
    const stand = standIn({ ...world, commands: [...(world.commands ?? []), read(['models', 'delete', 'llama3.2-1b-Q4KM'], deletePlanNone), read(['models', 'delete', 'llama3.2-1b-Q4KM', '--yes'], deleteNone)] })
    const model = new ModelsViewModel(stand.reads, stand.jobs, stand.clock)
    model.connect(stand.binary)
    await model.appeared()
    await model.select('llama3.2-1b-Q4KM')
    await model.act('deleteWithWeights')
    expect(model.confirmWords('model')?.alternative).toBe('')
    await model.confirm('model', true)
    expect(stand.child.calls.find((call) => call.arguments.includes('--yes'))?.arguments).toEqual(['models', 'delete', 'llama3.2-1b-Q4KM', '--yes'])
  })
})
