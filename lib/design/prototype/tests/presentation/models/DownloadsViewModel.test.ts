import { describe, expect, test } from 'vitest'
import { handles, llamaConversion, llamaPull, llamaQuantization } from '../../../source/presentation/models/Previews/ModelsWorlds'
import type { Landed } from '../../../source/presentation/models/ViewModels/StoreViewModel'
import { DownloadForm, DownloadsViewModel } from '../../../source/presentation/models/ViewModels/DownloadsViewModel'
import { ConvertForm, QuantizeForm } from '../../../source/presentation/models/ViewModels/StoreForms'
import { settled, standIn } from './ModelsHarness'
import convertLlama from '../../../fixtures/business/reads/models-convert-llama-handmade.txt?raw'
import { modelsWorld } from '../../../source/presentation/models/Previews/ModelsWorlds'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'

function downloads(world: 'store' | 'conversions' | 'fresh', apogee: ApogeeMode = 'installed') {
  const stand = standIn(world, {}, apogee)
  const model = new DownloadsViewModel(stand.jobs, stand.reads, stand.clock)
  model.connect(stand.child.locateBinary().case === 'found' ? stand.binary : undefined)
  const landed: Landed[][] = []
  const said: string[] = []
  model.landed = (sets) => landed.push([...sets])
  model.say = (text) => said.push(text)
  return { ...stand, model, landed, said }
}

const summary = (landed: readonly Landed[]) => landed.map((set) => [set.info.data?.handle, set.backend ?? 'none'])

describe('the Jobs tab: pulls and the store’s long runs, as supervised jobs (the Mac’s DownloadsViewModel)', () => {
  test('the download form: the CLI’s pull for each choice, the prototype’s steps, a reference needing its owner', () => {
    const form = new DownloadForm('meta-llama/Llama-3.2-1B-Instruct')
    expect(form.arguments).toEqual(['models', 'pull', 'meta-llama/Llama-3.2-1B-Instruct', '--safetensors', '--register-with', 'Q4_K_M'])
    expect(form.steps).toEqual(['Download the snapshot', 'Convert to GGUF F16', 'Quantize to Q4_K_M', 'Add it to the library'])
    form.quant = 'F16'
    expect(form.arguments.slice(-2)).toEqual(['--safetensors', '--register'])
    expect(form.steps).toEqual(['Download the snapshot', 'Convert to GGUF', 'Add it to the library'])
    form.setFormat('mlx')
    expect(form.arguments).toEqual(['models', 'pull', 'meta-llama/Llama-3.2-1B-Instruct', '--safetensors'])
    expect(form.canStart).toBe(true)
    form.repo = 'llama'
    expect(form.canStart).toBe(false)
    expect(form.facts).toBe('Use the owner/name form, e.g. Qwen/Qwen3-8B.')
  })

  test('a pull plays the CLI’s stream: its stages, its bytes, and, done, its three sets land with the backends its run registered', async () => {
    const { model, clock, child, landed, registry, said } = downloads('fresh')
    model.pull(new DownloadForm('meta-llama/Llama-3.2-1B-Instruct'))
    expect(child.calls.at(-1)?.arguments).toEqual(llamaPull)
    expect(model.running).toBe(1)
    expect(registry.children('job').map((job) => job.label)).toEqual(['apogee models pull meta-llama/Llama-3.2-1B-Instruct'])
    clock.advance(10)
    await settled()
    const card = model.face().cards[0]
    expect(card?.repo).toBe('meta-llama/Llama-3.2-1B-Instruct')
    expect(card?.status).toBe('running · step 1 of 4')
    expect(card?.steps[0]?.label).toBe("pull meta-llama/Llama-3.2-1B-Instruct's full weights")
    expect(card?.steps[0]?.detail, 'a quarter into model.safetensors').toBe('file 3 of 9 · 589 MiB of 2.3 GiB')
    expect(card?.steps.slice(1).map((step) => [step.label, step.state]), 'the stages not begun are the sheet’s planned ones').toEqual([['Convert to GGUF F16', 'waiting'], ['Quantize to Q4_K_M', 'waiting'], ['Add it to the library', 'waiting']])
    clock.advance(60)
    await settled()
    expect(model.face().cards[0]?.status).toMatch(/^done/)
    expect(model.face().cards[0]?.steps.map((step) => step.state)).toEqual(['done', 'done', 'done', 'done'])
    expect(summary(landed.at(-1) ?? [])).toEqual([
      [handles.llamaSnapshot, 'none'],
      [handles.llamaF16, 'Llama3.2-1B-Instruct-F16'],
      [handles.llamaQ4, 'Llama3.2-1B-Instruct-Q4KM'],
    ])
    expect(model.running).toBe(0)
    expect(registry.children()).toEqual([])
    expect(said, 'the old done toast, its model and level from the documents the run reported').toEqual(['Llama3.2-1B-Instruct · Q4_K_M is ready in the library'])
    expect(child.unanswered).toEqual([])
  })

  test('a conversion reads the store first, then runs; its steps tick on the sets its own run names, and they land registered', async () => {
    const { model, clock, child, landed, said } = downloads('conversions')
    model.convert(ConvertForm.gguf(handles.llamaSnapshot, 2482980600))
    await settled()
    expect(child.calls.map((call) => call.arguments.join(' '))).toEqual(['models list --all', llamaConversion.join(' ')])
    clock.advance(8)
    await settled()
    let card = model.face().cards[0]
    expect(card?.target).toBe('GGUF F16, Q4_K_M, registered')
    expect(card?.steps.map((step) => step.state)).toEqual(['done', 'running', 'waiting'])
    expect(card?.status).toBe('running · step 2 of 3')
    clock.advance(30)
    await settled()
    card = model.face().cards[0]
    expect(card?.steps.map((step) => [step.label, step.state, step.detail])).toEqual([
      ['Convert to GGUF F16', 'done', 'F16 · 2.3 GiB'],
      ['Quantize to Q4_K_M', 'done', 'Q4_K_M · 770 MiB'],
      ['Register 2 backends', 'done', 'Llama3.2-1B-Instruct-F16, Llama3.2-1B-Instruct-Q4KM'],
    ])
    expect(card?.openLabel).toBe('Show the weights')
    expect(summary(landed.at(-1) ?? [])).toEqual([
      [handles.llamaF16, 'Llama3.2-1B-Instruct-F16'],
      [handles.llamaQ4, 'Llama3.2-1B-Instruct-Q4KM'],
    ])
    expect(said, 'the old done toast: the sets its run named, the backends it registered').toEqual(['Llama3.2-1B-Instruct: 2 sets made, 2 registered'])
    expect(child.unanswered).toEqual([])
  })

  test('a set the run names in another model’s folder is never this conversion’s: a sibling job’s GGUF landing meanwhile ticks nothing (GUI 6d)', async () => {
    const world = modelsWorld('conversions')
    // Planted: the conversion's output naming a GGUF of another model first, as a sibling job's line would.
    const planted = convertLlama.replace('[1/3] convert to F16\n', '[1/3] convert to F16\n/Users/someone/.apogee/models/vast/gguf/eeeeeeeeeeee/vast-F16.gguf\n')
    const stand = standIn({ ...world, jobs: [{ arguments: llamaConversion, exact: true, answer: { stdout: planted } }] })
    const model = new DownloadsViewModel(stand.jobs, stand.reads, stand.clock)
    model.connect(stand.binary)
    model.convert(ConvertForm.gguf(handles.llamaSnapshot, 2482980600))
    await settled()
    stand.clock.advance(40)
    await settled()
    expect(model.downloads[0]?.named).toEqual([handles.llamaF16, handles.llamaQ4])
    expect(stand.child.calls.some((call) => call.arguments.join(' ') === 'models info /Users/someone/.apogee/models/vast/gguf/eeeeeeeeeeee/vast-F16.gguf'), 'it was asked, and placed elsewhere').toBe(true)
  })

  test('Stop while the store is read first ends the card with nothing started (GUI 6d): Run again reads it and runs', async () => {
    const { model, clock, child, landed, said } = downloads('store')
    const form = new QuantizeForm(handles.llamaF16, 'Llama3.2-1B-Instruct', 2479596089)
    const download = model.quantize(form)
    if (!download) throw new Error('a binary is found: the job starts')
    void model.stop(download)
    await settled()
    expect(child.calls.some((call) => call.kind === 'jobs')).toBe(false)
    let card = model.face().cards[0]
    expect(card?.status).toBe('stopped')
    expect(card?.outcome).toBe('Stopped before it ran: nothing ran')
    expect(card?.buttons).toEqual(['Run again', 'Dismiss'])
    model.again(download)
    await settled()
    expect(child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments)).toEqual([llamaQuantization])
    clock.advance(10)
    await settled()
    card = model.face().cards[0]
    expect(card?.status).toMatch(/^done/)
    expect(card?.outcome).toBe("added backend 'Llama3.2-1B-Instruct-Q4KM' to /Users/someone/.apogee/config/config.json")
    expect(summary(landed.at(-1) ?? [])).toEqual([[handles.llamaQ4, 'Llama3.2-1B-Instruct-Q4KM']])
    expect(said, 'stopped before it ran, it said nothing; its run again, done, the old toast').toEqual(['Llama3.2-1B-Instruct · Q4_K_M is ready in the library'])
    expect(child.unanswered).toEqual([])
  })

  test('Stop on a running job stops it, as Ctrl-C does; what it finished stands; Dismiss takes it off', async () => {
    const { model, clock, said } = downloads('fresh')
    const download = model.pull(new DownloadForm('meta-llama/Llama-3.2-1B-Instruct'))
    if (!download) throw new Error('a binary is found: the pull starts')
    clock.advance(30)
    await settled()
    await model.stop(download)
    await settled()
    const card = model.face().cards[0]
    expect(card?.status).toMatch(/^stopped at /)
    expect(card?.resume, 'the command its chain named for carrying on').toBe('Resume from convert')
    expect(said, 'a stopped run says nothing').toEqual([])
    model.dismiss(download)
    expect(model.face().cards).toEqual([])
  })

  test('Resume from … on a pull stopped after its snapshot runs the command its chain named, as the store job it is: the conversion lands its two sets with their backends', async () => {
    const { model, clock, child, landed, said } = downloads('fresh')
    const download = model.pull(new DownloadForm('meta-llama/Llama-3.2-1B-Instruct'))
    if (!download) throw new Error('a binary is found: the pull starts')
    clock.advance(30)
    await settled()
    await model.stop(download)
    await settled()
    expect(download.progress.resumeCommand).toEqual(llamaConversion)
    model.resume(download)
    await settled()
    expect(child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments), 'the pull, then the command its notice named, as typed').toEqual([llamaPull, llamaConversion])
    clock.advance(40)
    await settled()
    const card = model.face().cards[0]
    expect(card?.repo).toBe(handles.llamaSnapshot)
    expect(card?.status).toMatch(/^done/)
    expect(summary(landed.at(-1) ?? [])).toEqual([
      [handles.llamaF16, 'Llama3.2-1B-Instruct-F16'],
      [handles.llamaQ4, 'Llama3.2-1B-Instruct-Q4KM'],
    ])
    expect(said).toEqual(['Llama3.2-1B-Instruct: 2 sets made, 2 registered'])
    expect(child.unanswered).toEqual([])
  })

  test('with no binary nothing starts: no card is added, none runs, and nothing is asked (the Mac’s guard before its card)', async () => {
    const { model, child } = downloads('fresh', 'missing')
    expect(model.pull(new DownloadForm('meta-llama/Llama-3.2-1B-Instruct'))).toBeUndefined()
    expect(model.convert(ConvertForm.gguf(handles.llamaSnapshot, 2482980600))).toBeUndefined()
    expect(model.quantize(new QuantizeForm(handles.llamaF16, 'Llama3.2-1B-Instruct', 2479596089))).toBeUndefined()
    expect(model.check(handles.llamaF16)).toBeUndefined()
    await settled()
    expect(model.downloads).toEqual([])
    expect(model.running).toBe(0)
    expect(model.face()).toEqual({ cards: [], running: 0 })
    expect(child.calls).toEqual([])
  })

  test('a job nobody recorded never starts, and says so in the stand-in’s words', async () => {
    const { model, child } = downloads('fresh')
    model.pull(new DownloadForm('Qwen/Qwen3-8B'))
    await settled()
    const card = model.face().cards[0]
    expect(card?.status).toBe('failed')
    expect(card?.outcome).toBe('no recorded answer for apogee models pull Qwen/Qwen3-8B --safetensors --register-with Q4_K_M --output-format stream-json')
    expect(child.unanswered).toHaveLength(1)
  })

  test('the Jobs tab’s words, still: the prototype’s intro, a card’s log the run’s own lines', async () => {
    const { model, clock } = downloads('conversions')
    model.convert(ConvertForm.gguf(handles.llamaSnapshot, 2482980600))
    await settled()
    clock.advance(4)
    await settled()
    model.toggleLog(model.downloads[0]?.id ?? -1)
    const card = model.face().cards[0]
    expect(card?.logOpen).toBe(true)
    expect(card?.log.split('\n')[0]).toBe(`apogee ${llamaConversion.join(' ')}`)
    expect(card?.log).toContain('[1/3] convert to F16')
  })
})
