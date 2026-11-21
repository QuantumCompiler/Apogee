import { describe, expect, test } from 'vitest'
import { ModelInfo } from '../../../source/business/reads/Models'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import storeInfoF16 from '@recorded/business/reads/Fixtures/store-info-f16.json?raw'
import storeInfoQ4 from '@recorded/business/reads/Fixtures/store-info-q4.json?raw'
import deleteNone from '@recorded/business/reads/Fixtures/delete-none.txt?raw'
import deletePlanNone from '@recorded/business/reads/Fixtures/delete-plan-none.txt?raw'
import type { Rule } from '../../../source/infrastructure/process/StandInChild'
import { handles, modelsWorld } from '../../../source/presentation/models/Previews/ModelsWorlds'
import { StoreViewModel } from '../../../source/presentation/models/ViewModels/StoreViewModel'
import { standIn } from './ModelsHarness'

async function store(world: 'store' | 'conversions' | 'fresh' = 'store') {
  const stand = standIn(world)
  const model = new StoreViewModel(stand.reads)
  model.connect(stand.binary)
  await model.refresh()
  return { ...stand, model }
}

const infoOf = (text: string) => ModelInfo.read(ReadDocument.from(text) as ReadDocument)
const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })

describe('Stored weights: every set by handle, grouped by model, as the CLI lists them (the Mac’s StoreViewModel)', () => {
  test('the store world: the plain list’s sets and each backend’s set by its models info; two snapshots folded, as models list folds them', async () => {
    const { model, child } = await store()
    const face = model.face()
    expect(face.summary).toBe('9 models, 10 sets, in the model store')
    expect(face.groups.map((group) => group.dir)).toEqual(['Gemma4-12B', 'Gemma4-26B-Instruct', 'Gemma4-E4B', 'Gemma4-E4B-Assistant', 'Kokoro-82M', 'Llama3.2-1B-Instruct', 'Qwen3-8B', 'base', 'vast'])
    const llama = face.groups.find((group) => group.dir === 'Llama3.2-1B-Instruct')
    expect(llama?.sets.map((set) => [set.short, set.format, set.state])).toEqual([
      ['gguf/3f0c2a9b7e41', 'GGUF', 'not registered'],
      ['gguf/8d51e6c04a27', 'GGUF', 'backend llama3.2-1b-Q4KM'],
    ])
    expect(llama?.sets[1]?.fromText).toBe('quantized from Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41')
    expect(llama?.origin).toBe('from meta-llama/Llama-3.2-1B-Instruct')
    expect(face.groups.find((group) => group.dir === 'base')?.sets[0]?.state).toBe('backend helper, root, scribe')
    expect(face.fold).toEqual({ shown: true, text: '2 SafeTensors sets a conversion has used are folded away, as models list folds them.', button: 'Show them' })
    expect(face.registerAll).toEqual({ shown: true, label: 'Register the 2 not registered…' })
    expect(child.unanswered).toEqual([])
  })

  test('Show them reads models list --all: the snapshots a conversion used are shown, said used by a conversion', async () => {
    const { model, child } = await store()
    await model.setShowingAll(true)
    const llama = model.face().groups.find((group) => group.dir === 'Llama3.2-1B-Instruct')
    expect(llama?.sets.map((set) => [set.short, set.state])).toContainEqual(['safetensors/edaba290ce7f', 'used by a conversion'])
    expect(model.face().fold.button).toBe('Fold them')
    // The Gemma 4 26B snapshot's models info is the one read nobody recorded: its facts are said unread, in the stand-in's words.
    expect(child.unanswered).toEqual(['reads: apogee models info Gemma4-26B-Instruct/safetensors/13de95725073'])
  })

  test('selecting a set: its details from its models info, the CLI’s words, its actions by what it is; selecting it again closes them', async () => {
    const { model } = await store()
    await model.toggle(handles.llamaF16)
    const detail = model.face().detail
    expect(detail.shown).toBe(true)
    expect(detail.handle).toBe(handles.llamaF16)
    expect(detail.state).toBe('not registered')
    expect(detail.actions.map((action) => action.label)).toEqual(['Quantize…', 'Register…', 'Check the files', 'Show in Finder', 'Delete…'])
    expect(detail.facts).toContainEqual({ label: 'On disk', value: '2.3 GiB', mono: false })
    expect(detail.facts).toContainEqual({ label: 'Came from', value: 'converted from Llama3.2-1B-Instruct/safetensors/edaba290ce7f (recorded)', mono: false })
    expect(detail.note).toBe('Stored but not registered, so no session can use it yet. Registering writes one config entry; nothing is copied or loaded.')
    expect(detail.children.map((child) => child.label)).toEqual([handles.llamaQ4])
    expect(detail.command).toBe(`apogee models info ${handles.llamaF16} --output-format json`)
    await model.toggle(handles.llamaF16)
    expect(model.face().detail.shown).toBe(false)
  })

  test('the conversions world: the Llama snapshot unconverted, weights only, its two conversions offered as its models info states them; the draft head offers none', async () => {
    const { model } = await store('conversions')
    await model.toggle(handles.llamaSnapshot)
    const detail = model.face().detail
    expect(detail.state).toBe('weights only · convert to run')
    expect(detail.actions.map((action) => action.label)).toEqual(['Convert to GGUF…', 'Convert to MLX…', 'Check the files', 'Show in Finder', 'Delete…'])
    await model.toggle('Gemma4-12B-Assistant/safetensors/43b42c6289a0')
    expect(model.face().detail.actions.map((action) => action.label)).toEqual(['Check the files', 'Show in Finder', 'Delete…'])
  })

  test('Convert to GGUF… opens the sheet on the set; a backend name already taken refuses; Convert hands the form to the jobs and closes it', async () => {
    const { model } = await store('conversions')
    const started: string[][] = []
    const presented: string[] = []
    model.present = (kind) => presented.push(kind)
    model.startConvert = (form) => started.push(form.arguments)
    await model.toggle(handles.llamaSnapshot)
    model.act('convertToGGUF')
    expect(presented).toEqual(['storeConvert'])
    const sheet = model.sheet()
    expect(sheet?.kind).toBe('convert')
    expect(sheet?.title).toBe('Convert Llama3.2-1B-Instruct')
    expect(sheet?.command).toBe(`apogee models convert ${handles.llamaSnapshot} --register-with Q4_K_M`)
    expect(sheet?.canGo).toBe(true)
    model.retarget(true)
    model.editConvert((form) => (form.baseName = 'Gemma4-E4B'))
    expect(model.sheet()?.errors).toEqual(['A backend named Gemma4-E4B-4bit exists: choose another beginning.'])
    expect(model.sheet()?.canGo).toBe(false)
    model.retarget(false)
    model.editConvert((form) => (form.baseName = 'Llama3.2-1B-Instruct'))
    expect(model.sheet()?.errors).toEqual([])
    model.go()
    expect(started).toEqual([[...['models', 'convert', handles.llamaSnapshot, '--register-with', 'Q4_K_M']]])
    expect(model.sheet()).toBeUndefined()
  })

  test('a taken name refuses in each sheet: register an MLX set under a backend’s name', async () => {
    const { model } = await store()
    await model.toggle(handles.gemmaMlx)
    await model.actAsync('register')
    expect(model.sheet()?.title).toBe('Register Gemma4-26B-Instruct · mixed 4/8-bit (group 64)')
    expect(model.register?.name, 'the name config scan lists it under').toBe('Gemma4-26B-Instruct-mixed_4_8')
    expect(model.sheet()?.canGo).toBe(true)
    model.editRegister((form) => (form.name = 'llama3.2-1b-Q4KM'))
    expect(model.sheet()?.errors).toEqual(['A backend named llama3.2-1b-Q4KM exists: choose another name.'])
    expect(model.sheet()?.canGo).toBe(false)
  })

  test('Register runs config add-backend, read back, its line said', async () => {
    const { model, child } = await store()
    const said: string[] = []
    model.say = (text) => said.push(text)
    await model.toggle(handles.gemmaMlx)
    await model.actAsync('register')
    await model.goAsync()
    expect(child.calls.find((call) => call.arguments[1] === 'add-backend')?.arguments.slice(0, 3)).toEqual(['config', 'add-backend', 'Gemma4-26B-Instruct-mixed_4_8'])
    expect(said).toEqual(['Registered Gemma4-26B-Instruct-mixed_4_8'])
    expect(child.unanswered).toEqual([])
  })

  test('delete asks with the CLI’s own plan; its second door only where the plan names a backend; yes runs --yes, the other --yes --and-backends, and what it printed is kept', async () => {
    const { model, child } = await store()
    const asked: string[] = []
    model.ask = (kind) => asked.push(kind)
    await model.toggle(handles.llamaQ4)
    await model.actAsync('delete')
    expect(asked).toEqual(['storeDelete'])
    const words = model.confirmWords()
    expect(words?.title).toBe(`Delete ${handles.llamaQ4}?`)
    expect(words?.detail).toContain("backend 'llama3.2-1b-Q4KM' points into this")
    expect(words?.detail.endsWith(`apogee models delete ${handles.llamaQ4} --yes -- this can't be undone.`)).toBe(true)
    expect(words?.alternative).toBe('Delete it and the backends that run from it')
    await model.confirm(true)
    expect(child.calls.find((call) => call.arguments.includes('--yes'))?.arguments).toEqual(['models', 'delete', handles.llamaQ4, '--yes', '--and-backends'])
    expect(model.face().narration).toEqual({ shown: true, title: `Deleted ${handles.llamaQ4}`, text: expect.stringContaining("removed backend 'llama3.2-1b-Q4KM'") })
    model.dismissNarration()
    expect(model.face().narration.shown).toBe(false)
  })

  test('a delete whose plan names no backend offers one door; one the CLI refuses asks nothing and says why', async () => {
    // The unregistered F16's delete answered with the CLI's recorded plan that names no backend, and its --yes (the later of two rules as long wins).
    const world = modelsWorld('store')
    const stand = standIn({ ...world, commands: [...(world.commands ?? []), read(['models', 'delete', handles.llamaF16], deletePlanNone), read(['models', 'delete', handles.llamaF16, '--yes'], deleteNone)] })
    const unnamed = new StoreViewModel(stand.reads)
    unnamed.connect(stand.binary)
    await unnamed.refresh()
    await unnamed.toggle(handles.llamaF16)
    await unnamed.actAsync('delete')
    expect(unnamed.confirmWords()?.alternative).toBe('')
    await unnamed.confirm(true)
    expect(stand.child.calls.find((call) => call.arguments.includes('--yes'))?.arguments, 'its second door, asked for, still runs --yes alone').toEqual(['models', 'delete', handles.llamaF16, '--yes'])
    const { model } = await store()
    await model.toggle('Kokoro-82M/voice/5f3a2c91d0e4')
    await model.actAsync('delete')
    expect(model.pending).toBeUndefined()
    expect(model.face().failure).toBe('no recorded answer for apogee models delete Kokoro-82M/voice/5f3a2c91d0e4')
  })

  test('Delete all of it… asks with the plan of the whole model; plain yes keeps the backend, as the CLI says', async () => {
    const { model, child } = await store()
    await model.toggle(handles.llamaF16)
    await model.askToDeleteModel('Llama3.2-1B-Instruct')
    expect(model.confirmWords()?.title).toBe('Delete all of Llama3.2-1B-Instruct?')
    await model.confirm(false)
    expect(child.calls.find((call) => call.arguments.includes('--yes'))?.arguments).toEqual(['models', 'delete', 'Llama3.2-1B-Instruct', '--yes'])
    expect(model.face().narration.text).toContain("kept backend 'llama3.2-1b-Q4KM'")
  })

  test('register all: the unregistered runnable sets by handle, then config scan --register; not destructive', async () => {
    const { model, child } = await store()
    await model.askToRegisterAll()
    const words = model.confirmWords()
    expect(words?.title).toBe('Register 2 stored models?')
    expect(words?.detail).toBe(`${handles.gemmaMlx}\n${handles.llamaF16}\n\napogee config scan --register`)
    expect(words?.destructive).toBe(false)
    await model.confirm(false)
    expect(child.calls.find((call) => call.arguments.includes('--register'))?.arguments).toEqual(['config', 'scan', '--register'])
    expect(model.face().narration.text).toContain("added backend 'Gemma4-26B-Instruct-mixed_4_8'")
  })

  test('what a job reports it made lands in Stored weights: its sets by their documents, the backends its run registered; a snapshot a conversion used folds', async () => {
    const { model } = await store('conversions')
    model.land([
      { info: infoOf(storeInfoF16), backend: 'Llama3.2-1B-Instruct-F16' },
      { info: infoOf(storeInfoQ4), backend: 'Llama3.2-1B-Instruct-Q4KM' },
    ])
    const llama = model.face().groups.find((group) => group.dir === 'Llama3.2-1B-Instruct')
    expect(llama?.sets.map((set) => [set.short, set.state])).toEqual([
      ['gguf/3f0c2a9b7e41', 'backend Llama3.2-1B-Instruct-F16'],
      ['gguf/8d51e6c04a27', 'backend Llama3.2-1B-Instruct-Q4KM'],
    ])
    expect(model.face().fold.text).toBe('1 SafeTensors set a conversion has used is folded away, as models list folds it.')
    await model.refresh()
    expect(model.face().groups.find((group) => group.dir === 'Llama3.2-1B-Instruct')?.sets).toHaveLength(2)
  })

  test('a set a job is working on is busy: its state the job’s, its actions waiting', async () => {
    const { model } = await store('conversions')
    model.busy = (handle) => (handle === handles.llamaSnapshot ? 'convert' : undefined)
    await model.toggle(handles.llamaSnapshot)
    const row = model.face().groups.find((group) => group.dir === 'Llama3.2-1B-Instruct')?.sets[0]
    expect(row?.state).toBe('converting')
    expect(row?.busy).toBe(true)
    expect(model.face().detail.actions.every((action) => action.disabled)).toBe(true)
    expect(model.face().detail.note).toBe('A job is working on it; see Jobs.')
  })
})
