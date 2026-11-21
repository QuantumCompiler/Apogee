import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import generateRun from '../../../fixtures/business/media/video-generate-run-handmade.jsonl?raw'
import generateRain from '../../../fixtures/business/media/video-generate-rain-handmade.jsonl?raw'
import refusedPeft from '../../../fixtures/business/media/generate-refused-peft-handmade.jsonl?raw'
import refusedPeftErr from '../../../fixtures/business/media/generate-refused-peft-handmade.err?raw'
import clipExtensionRecord from '../../../fixtures/business/media/clip-lighthouse-dusk-2-handmade.json?raw'
import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import { StandInFolders } from '../../../source/infrastructure/process/StandInFolders'
import type { Folders } from '../../../source/infrastructure/process/StandInFolders'
import { clipPath, clips, finalFromDraft, installVideo, paperBoat, rainAgain, videoFolders, videoFoldersWithoutClip, videoWorld } from '../../../source/presentation/video/Previews/VideoWorlds'
import { VideoForm } from '../../../source/presentation/video/ViewModels/VideoForm'
import { VideoViewModel } from '../../../source/presentation/video/ViewModels/VideoViewModel'

/** Lets the replayed jobs' updates land: each `for await` takes a turn of the event loop. */
const settle = async (rounds = 4) => {
  for (let round = 0; round < rounds; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The place over the video world, read, on a clock the test moves; what it said, and what the shell was asked. */
async function place(options: { readonly rules?: Rules; readonly folders?: Folders; readonly apogee?: ApogeeMode } = {}) {
  const child = new StandInChild({ rules: options.rules ?? videoWorld(), apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
  const clock = new Clock({})
  const registry = new ChildRegistry()
  const video = new VideoViewModel(new ReadRunner(child), new JobStarter(child, clock, registry), new StandInFolders({ folders: options.folders ?? videoFolders(), apogee: options.apogee ?? 'installed' }), clock)
  const said: string[] = []
  const sheets: string[] = []
  video.say = (text) => said.push(text)
  video.present = (sheet) => sheets.push(sheet)
  video.dismissSheet = () => sheets.push('dismissed')
  video.registry = registry
  video.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  await video.appeared()
  return { video, child, clock, registry, said, sheets }
}

/** Ticks the clock `n` times, letting each tick's lines land. */
async function tick(clock: Clock, n: number) {
  for (let at = 0; at < n; at += 1) {
    clock.advance(1)
    await settle(2)
  }
  await settle()
}

/** Rain's clip, which uses a LoRA. */
const rainRecord = () => clipPath(clips.rain)

describe('the Video place: its clips, a clip’s page, the generate form, a run and the install (design 1k, the Mac’s VideoViewModel)', () => {
  test('the library: the five records in ~/Movies/Apogee, newest first, the newest shown; the store’s pipeline and MLX model offered, the default its MLX', async () => {
    const { video } = await place()
    expect(video.clips.map((clip) => clip.name)).toEqual([clips.extension, clips.final, clips.draft, clips.harbor, clips.rain])
    const face = video.face()
    expect(face.tabs.map((tab) => `${tab.label} ${tab.count}`)).toEqual(['Clips 5', 'Compositions '])
    expect(face.pipelineLine).toBe('Wan2.2-TI2V-5B · MLX · default')
    expect(face.clip?.file).toBe(clips.extension)
    expect(face.list.map((row) => `${row.file} ${row.tag} ${row.meta}`)).toEqual([
      'lighthouse-dusk-2.mp4 FINAL 5 s · seed 918273 · Today 11:12',
      'lighthouse-dusk.mp4 FINAL 5 s · seed 4242 · Today 10:58',
      'lighthouse-dusk-draft.mp4 DRAFT 5 s · seed 4242 · Today 10:41',
      'harbor-first-light.mp4 FINAL 5 s · seed 77 · Yesterday 18:20',
      'rain-window.mp4 40 STEPS 4 s · seed 1203 · Oct 16',
    ])
    expect(face.form.pipelines.map((choice) => choice.label)).toEqual(['Wan2.2-TI2V-5B · PyTorch', 'Wan2.2-TI2V-5B · MLX · default'])
    expect(face.form.pipeline).toBe('Wan2.2-TI2V-5B/mlx/5e6f708192a3')
  })

  test('a clip’s page: its record’s facts in the CLI’s words, the chain back to its draft, its files and the command that runs it again', async () => {
    const { video } = await place()
    video.select(clipPath(clips.final))
    const clip = video.face().clip
    expect(clip?.modeText).toBe('from text')
    expect(clip?.facts.map((fact) => `${fact.label}: ${fact.value}`)).toEqual([
      'Pipeline: Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6 · WanPipeline',
      'Made: from text',
      'Seed: 4242 · held',
      'Frame: 1280×704 · 24 fps',
      'Length: 5 s · 121 frames',
      'Steps: 50 · the final preset',
      'Guidance: the pipeline’s own default',
      'Device: mps · bfloat16',
      'Versions: PyTorch · diffusers 0.35.1 · torch 2.8.0',
      'Codec: h264',
      'Built by: apogee 0.1.9 · macos-arm64',
      'Took: 7 min 48 s',
    ])
    expect(clip?.chain).toEqual([
      { label: clips.final, how: 'run again from lighthouse-dusk-draft.mp4.json, seed held, 50 steps · Today 10:58', go: '' },
      { label: clips.draft, how: 'from text, seed 4242 generated, 16 steps · Today 10:41', go: clipPath(clips.draft) },
    ])
    expect(clip?.command).toBe('apogee video generate --from ~/Movies/Apogee/lighthouse-dusk.mp4.json -o ~/Movies/Apogee/lighthouse-dusk-3.mp4')
    video.select(clipPath(clips.rain))
    const rain = video.face().clip
    expect(rain?.note).toBe('Recorded on linux-x86_64 (cuda · bfloat16). This is macos-arm64, so run here, the same seed may not make the same clip.')
    expect(rain?.loras).toEqual([{ path: '~/loras/rain-glass-wan22.safetensors', scale: '0.7', sha: 'c41d8e02…5f7a', adapter: 'lora1' }])
    expect(rain?.facts.find((fact) => fact.label === 'Made')?.value).toBe('from ~/Pictures/window-still.jpg')
  })

  test('the default form: a paper boat’s draft, its command drawn in the prototype’s words and run in the CLI’s verdict order', async () => {
    const { video } = await place()
    video.edit({ prompt: paperBoat })
    const form = video.face().form
    expect(form.command).toBe('apogee video generate "A paper boat drifting down a rain-filled gutter, low angle, shallow depth of field" --draft -o ~/Movies/Apogee/paper-boat-drifting-draft.mp4')
    expect(form.out).toBe('paper-boat-drifting-draft.mp4')
    expect(VideoForm.clipForm(video.form, undefined, video.context).arguments('/x.mp4')).toEqual(['video', 'generate', '-o', '/x.mp4', '--draft', '--', paperBoat])
    // A new clip's form draws no told box, as the old one: the CLI's preset line is the run's to say.
    expect(form.told).toBe('')
    expect(VideoForm.told(video.form, undefined, video.context)).toEqual(['preset: draft -- 16 steps'])
    // The store's pipeline picked: its folder holds its MLX conversion too, so the engine is said.
    video.edit({ pipeline: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6' })
    expect(video.face().form.command).toBe('apogee video generate "A paper boat drifting down a rain-filled gutter, low angle, shallow depth of field" --model Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6 --engine torch --draft -o ~/Movies/Apogee/paper-boat-drifting-draft.mp4')
  })

  test('the form’s errors, in the old prototype’s words', async () => {
    const { video } = await place()
    video.edit({ prompt: 'x', dur: '12', fps: '2', steps: '0.5', out: 'lighthouse-dusk.mp4' })
    expect(video.face().form.errors).toEqual(['A clip is 10 s at most. Longer videos are compositions.', 'The frame rate is 4 to 60 fps.', 'Steps are a whole number from 1 to 100.', '~/Movies/Apogee/lighthouse-dusk.mp4 exists. A generation never writes over a file; name another.'])
    expect(video.face().form.canGenerate).toBe(false)
  })

  test('the told box says what the CLI will say: the draft made final names what it takes and what changed, as its run’s stream does', async () => {
    const { video } = await place()
    video.act('final', clipPath(clips.draft))
    const told = video.face().form.told.split('\n')
    const notices = generateRun
      .split('\n')
      .filter((line) => line.includes('"notice"'))
      .map((line) => (JSON.parse(line) as { text: string }).text)
    expect(told).toEqual(notices.slice(0, 2))
    video.act('again', clipPath(clips.rain))
    const rainNotices = generateRain
      .split('\n')
      .filter((line) => line.includes('"notice"'))
      .map((line) => (JSON.parse(line) as { text: string }).text)
    expect(video.face().form.told.split('\n')).toEqual(rainNotices.slice(0, 3))
  })

  test('a run: the draft made final plays its stream -- the pipeline loads, it denoises step by step, it assembles -- and its clip lands, its record beside it', async () => {
    const { video, clock, registry, said } = await place()
    video.act('final', clipPath(clips.draft))
    video.generate()
    expect(registry.children('job').map((job) => job.label)).toEqual(['apogee video generate'])
    expect(registry.children('job')[0]?.arguments).toEqual([...finalFromDraft, '--output-format', 'stream-json'])
    await tick(clock, 43)
    const run = video.face().run
    expect(video.face().showRun).toBe(true)
    expect(run?.status).toBe('step 38 of 50')
    expect(run?.phases.map((phase) => `${phase.label} ${phase.status} ${phase.detail}`)).toEqual(['Load the pipeline done Wan2.2-TI2V-5B · PyTorch', 'Denoise running step 38 of 50', 'Assemble the clip pending 121 frames · H.264'])
    expect(run?.log.split('\n').slice(3)).toEqual(['running on mps (bfloat16) -- expect minutes per clip on Apple silicon', 'step 1/50', 'step 5/50', 'step 10/50', 'step 15/50', 'step 20/50', 'step 25/50', 'step 30/50', 'step 35/50'])
    expect(video.face().list[0]).toMatchObject({ file: 'lighthouse-dusk-3.mp4', tag: 'RUNNING', meta: 'step 38 of 50', running: true })
    expect(video.face().form.goNote).toBe('One clip at a time: lighthouse-dusk-3.mp4 is generating.')
    await tick(clock, 18)
    expect(said).toContain('lighthouse-dusk-3.mp4 is ready, with its record beside it')
    expect(video.face().run.shown).toBe(false)
    expect(video.clips[0]?.name).toBe('lighthouse-dusk-3.mp4')
    expect(video.face().clip?.file).toBe('lighthouse-dusk-3.mp4')
    expect(registry.children('job')).toEqual([])
  })

  test('Stop on a run leaves no clip and no record: the CLI’s cancel said, nothing landed, the list as it was', async () => {
    const { video, clock } = await place()
    video.act('final', clipPath(clips.draft))
    video.generate()
    await tick(clock, 12)
    await video.stop()
    await settle()
    const face = video.face()
    expect(face.run?.status).toBe('stopped by you')
    expect(face.run?.foot).toBe('Nothing was left behind: no clip, no record, and the working folder is gone.')
    expect(face.run?.log.split('\n').at(-1)).toBe('cancelled -- nothing was written (/Users/someone/Movies/Apogee/lighthouse-dusk-3.mp4 untouched)')
    expect(face.list[0]).toMatchObject({ tag: 'STOPPED', meta: 'nothing written' })
    await tick(clock, 60)
    expect(video.clips.map((clip) => clip.name)).toEqual([clips.extension, clips.final, clips.draft, clips.harbor, clips.rain])
    video.dismissRun()
    expect(video.face().run.shown).toBe(false)
  })

  test('a generate refused for its runtime opens the consent with the CLI’s line; Decline starts nothing', async () => {
    const { video, clock, child, registry, said, sheets } = await place()
    video.act('again', rainRecord())
    video.generate()
    await tick(clock, 3)
    expect(sheets).toEqual(['runtimeConsent'])
    expect(video.face().consent).toEqual({ shown: true, title: 'Install what video needs?', said: 'a LoRA loads through peft, which the video runtime does not have yet -- apogee train setup --with video', body: "This downloads and installs Python packages into Apogee's own environment, once. Nothing is installed unless you say yes; the install runs here, with Stop, and what was refused runs again once it is in.", command: 'apogee train setup --with video', confirm: 'Install', cancel: 'Not now' })
    // The CLI refused before it ran anything: no run is drawn.
    expect(video.face().run.shown).toBe(false)
    video.decline()
    expect(said.at(-1)).toBe('Not installed. apogee train setup --with video installs it whenever you want it')
    expect(video.face().consent.shown).toBe(false)
    await tick(clock, 20)
    expect(child.calls.filter((call) => call.arguments[0] === 'train')).toEqual([])
    expect(registry.children()).toEqual([])
    expect(video.face().form.goNote).toBe('LoRAs need peft; Generate asks first.')
  })

  test('Install runs once and fails in the CLI’s words; Generate asks again, Install goes in and the refused clip is made', async () => {
    const { video, clock, child, registry, said } = await place()
    video.act('again', rainRecord())
    video.generate()
    await tick(clock, 3)
    video.install()
    expect(registry.children('job').map((job) => job.label)).toEqual(['apogee train setup'])
    await tick(clock, 4)
    expect(video.face().install).toEqual({ shown: true, state: 'running', headline: 'Installing the video set', detail: "installing the 'video' set: torch>=2.6 diffusers>=0.35 transformers>=4.46 accelerate>=1.0 ftfy>=6.1 sentencepiece>=0.2 peft>=0.13 ...", command: 'apogee train setup --with video' })
    // Generate while it installs starts nothing, and says why.
    video.generate()
    expect(said.at(-1)).toBe('Installing first: the clip can be made once apogee train setup --with video has finished')
    await tick(clock, 8)
    expect(video.face().install).toMatchObject({ state: 'failed', headline: 'The video set did not install', detail: "apogee train: pip install for the 'video' set failed (exit code 1):\nERROR: Could not install packages: this sandbox's pip is a stub with no network" })
    expect(said.at(-1)).toBe("Not installed: apogee train: pip install for the 'video' set failed (exit code 1):")
    expect(child.calls.filter((call) => call.arguments[1] === 'generate')).toHaveLength(1)
    video.installAct()
    expect(video.face().install.shown).toBe(false)
    // Again: the CLI refuses again, the consent asks again, and this install goes in.
    video.generate()
    await tick(clock, 3)
    expect(video.face().consent?.command).toBe('apogee train setup --with video')
    video.install()
    await tick(clock, 12)
    expect(said).toContain('Installed: apogee train setup --with video -- making the clip again')
    expect(video.face().install?.state).toBe('installed')
    expect(child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments.join(' '))).toEqual([
      [...rainAgain, '--output-format', 'stream-json'].join(' '),
      installVideo.join(' '),
      [...rainAgain, '--output-format', 'stream-json'].join(' '),
      installVideo.join(' '),
      [...rainAgain, '--output-format', 'stream-json'].join(' '),
    ])
    expect(video.face().run?.status).toMatch(/^(loading the pipeline|step \d+ of 40)$/)
    await tick(clock, 52)
    expect(said).toContain('rain-window-2.mp4 is ready, with its record beside it')
    expect(video.clips[0]?.name).toBe('rain-window-2.mp4')
    expect(video.clips[0]?.versions.peft).toBe('0.17.1')
  })

  test('rain is refused until the install has gone in, however often Generate is pressed: Not now three times, three consents, no clip', async () => {
    const { video, clock, child, sheets } = await place()
    video.act('again', rainRecord())
    for (let press = 0; press < 3; press += 1) {
      video.generate()
      await tick(clock, 3)
      expect(video.face().consent.shown).toBe(true)
      video.decline()
    }
    await tick(clock, 60)
    expect(video.clips.map((clip) => clip.name)).not.toContain('rain-window-2.mp4')
    expect(child.calls.filter((call) => call.arguments[0] === 'train')).toEqual([])
    expect(sheets.filter((sheet) => sheet === 'runtimeConsent')).toHaveLength(3)
  })

  test('an install from Settings that fails, then the consent’s that goes in: rain’s clip is made at once, with no second consent', async () => {
    const { video, clock, said, sheets } = await place()
    video.ask({ arguments: installVideo, said: 'Video · runtime: not set up' })
    video.install()
    await tick(clock, 12)
    expect(video.face().install.state).toBe('failed')
    video.installAct()
    video.act('again', rainRecord())
    video.generate()
    await tick(clock, 3)
    expect(video.face().consent.shown).toBe(true)
    video.install()
    await tick(clock, 12)
    expect(said).toContain('Installed: apogee train setup --with video -- making the clip again')
    expect(video.face().consent.shown).toBe(false)
    await tick(clock, 52)
    expect(video.clips[0]?.name).toBe('rain-window-2.mp4')
    expect(sheets.filter((sheet) => sheet === 'runtimeConsent')).toHaveLength(2)
  })

  test('the install that goes in rests on its installing line while it runs; Stop leaves that line, and the clip is refused again; run to its end, it says where it went', async () => {
    const { video, clock, said } = await place()
    video.act('again', rainRecord())
    video.generate()
    await tick(clock, 3)
    video.install()
    await tick(clock, 12)
    video.installAct()
    video.generate()
    await tick(clock, 3)
    video.install()
    await tick(clock, 6)
    const installing = "installing the 'video' set: torch>=2.6 diffusers>=0.35 transformers>=4.46 accelerate>=1.0 ftfy>=6.1 sentencepiece>=0.2 peft>=0.13 ..."
    expect(video.face().install).toMatchObject({ state: 'running', detail: installing })
    await video.stop()
    await settle()
    expect(video.face().install).toMatchObject({ state: 'stopped', detail: installing })
    // Stopped, it did not go in: the CLI refuses rain again, and asks.
    video.installAct()
    video.generate()
    await tick(clock, 3)
    expect(video.face().consent.shown).toBe(true)
    expect(said.filter((line) => line.startsWith('Installed:'))).toEqual([])
    video.install()
    await tick(clock, 6)
    expect(video.face().install).toMatchObject({ state: 'running', detail: installing })
    await tick(clock, 5)
    expect(video.face().install).toMatchObject({ state: 'installed', detail: 'python env: at /Users/someone/.apogee/training/venv; sets: video' })
    expect(said).toContain('Installed: apogee train setup --with video -- making the clip again')
  })

  test('Install asked again while this place’s own install runs starts nothing: it names that install, and the consent stays', async () => {
    const { video, clock, registry, said } = await place()
    video.ask({ arguments: installVideo, said: 'Video · runtime: not set up' })
    video.install()
    await tick(clock, 2)
    video.ask({ arguments: installVideo, said: 'Video · runtime: not set up' })
    video.install()
    expect(said.at(-1)).toBe('apogee train setup --with video is already running: press Install again once it has finished')
    expect(video.face().consent.shown).toBe(true)
    expect(registry.children('job').map((job) => job.label)).toEqual(['apogee train setup'])
  })

  test('Install asked from Settings while a clip is made starts the install beside it: only another install, or a rebuild over a run, waits', async () => {
    const { video, clock, registry, sheets } = await place()
    video.act('final', clipPath(clips.draft))
    video.generate()
    await tick(clock, 2)
    video.ask({ arguments: installVideo, said: 'Video · runtime: not set up' })
    video.install()
    expect(sheets.at(-1)).toBe('dismissed')
    expect(video.face().consent.shown).toBe(false)
    expect(registry.children('job').map((job) => job.label)).toEqual(['apogee video generate', 'apogee train setup'])
    expect(video.face().run.status).toMatch(/^(loading the pipeline|step \d+ of 50)$/)
  })

  test('the file field empties: it shows what was typed, the form asks for a name, Generate is off, and the command ends in …', async () => {
    const { video } = await place()
    video.edit({ prompt: paperBoat })
    video.edit({ out: '' })
    const form = video.face().form
    expect(form.out).toBe('')
    expect(form.errors).toEqual(['Name the file to write.'])
    expect(form.canGenerate).toBe(false)
    expect(form.command.endsWith(' -o ~/Movies/Apogee/…')).toBe(true)
    // Typed with a trailing space: the field keeps it while typing; the command and the run take it trimmed.
    video.edit({ out: 'boat.mp4 ' })
    expect(video.face().form.out).toBe('boat.mp4 ')
    expect(video.face().form.command.endsWith(' -o ~/Movies/Apogee/boat.mp4')).toBe(true)
  })

  test('a new clip with a LoRA, refused for peft and made again once the install is in, keeps its prompt and its pipeline on the run card and in the list', async () => {
    const draft = await place()
    draft.video.startFresh()
    draft.video.edit({ prompt: paperBoat, pipeline: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6' })
    draft.video.addLora()
    const output = VideoForm.output(draft.video.form, undefined, draft.video.context.taken)
    const args = VideoForm.clipForm(draft.video.form, undefined, draft.video.context).arguments(clipPath(output)) ?? []
    const at = args.indexOf('--')
    const streamedArgs = [...args.slice(0, at), '--output-format', 'stream-json', ...args.slice(at)]
    const world = videoWorld()
    const rules: Rules = { ...world, jobs: [...world.jobs, { arguments: streamedArgs, exact: true, until: { ran: installVideo, answer: { stdout: refusedPeft, stderr: refusedPeftErr, exit: 1 } }, answer: { stdout: generateRun } }] }
    const { video, clock } = await place({ rules })
    video.startFresh()
    video.edit({ prompt: paperBoat, pipeline: 'Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6' })
    video.addLora()
    video.generate()
    await tick(clock, 3)
    expect(video.face().consent.shown).toBe(true)
    // The first install fails, as the old one's does; Generate again is refused again, and the install that goes in runs it again.
    video.install()
    await tick(clock, 12)
    video.installAct()
    video.generate()
    await tick(clock, 3)
    video.install()
    await tick(clock, 13)
    const run = video.face().run
    expect(run).toMatchObject({ shown: true, file: output, prompt: paperBoat })
    expect(run.phases[0]?.detail).toBe('Wan2.2-TI2V-5B · PyTorch')
    expect(video.face().list[0]).toMatchObject({ id: 'run', file: output, prompt: paperBoat })
  })

  test('a run’s Denoise and Assemble say its own plan, not the form’s as it is edited after: 50 steps and 121 frames, whatever the form now holds', async () => {
    const { video, clock } = await place()
    video.act('final', clipPath(clips.draft))
    video.generate()
    await tick(clock, 1)
    video.act('again', rainRecord())
    expect(video.face().run.phases.slice(1).map((phase) => phase.detail)).toEqual(['step 0 of 50', '121 frames · H.264'])
    video.startFresh()
    expect(video.face().run.phases.slice(1).map((phase) => phase.detail)).toEqual(['step 0 of 50', '121 frames · H.264'])
  })

  test('the clip being made, as the monitor names it: its job’s arguments always, and what it is only once its pipeline has loaded -- not while it loads, nor while a refusal plays', async () => {
    const { video, clock } = await place()
    video.act('final', clipPath(clips.draft))
    video.generate()
    const draftArguments = [...finalFromDraft, '--output-format', 'stream-json']
    await tick(clock, 4)
    expect(video.generating).toEqual({ arguments: draftArguments, loaded: undefined })
    await tick(clock, 1)
    expect(video.generating).toEqual({ arguments: draftArguments, loaded: { name: 'Wan2.2-TI2V-5B · generating', runtime: 'mps · bfloat16' } })
    await video.stop()
    await settle()
    expect(video.generating).toBeUndefined()
    video.dismissRun()
    video.act('again', rainRecord())
    video.generate()
    await tick(clock, 1)
    expect(video.generating).toEqual({ arguments: [...rainAgain, '--output-format', 'stream-json'], loaded: undefined })
    await tick(clock, 3)
    expect(video.generating).toBeUndefined()
  })

  test('a record run again from a continuation continues that clip again, as the CLI keeps `continued`: its page, its Made, and its form say so', async () => {
    const extension = JSON.parse(clipExtensionRecord) as Record<string, unknown>
    const again = {
      ...extension,
      created_at: '2026-11-14T11:30:00Z',
      output: clipPath('lighthouse-dusk-4.mp4'),
      sidecar: `${clipPath('lighthouse-dusk-4.mp4')}.json`,
      parent: { move: 'from', document: `${clipPath(clips.extension)}.json`, clip: clipPath(clips.extension), continued: clipPath(clips.final) },
    }
    const folders = videoFolders()
    const folder = Object.keys(folders)[0] ?? ''
    const { video } = await place({ folders: { [folder]: [...(folders[folder] ?? []), { name: 'lighthouse-dusk-4.mp4' }, { name: 'lighthouse-dusk-4.mp4.json', text: JSON.stringify(again) }] } })
    video.select(clipPath('lighthouse-dusk-4.mp4'))
    const clip = video.face().clip
    expect(clip.modeText).toBe('continues lighthouse-dusk.mp4')
    expect(clip.facts.find((fact) => fact.label === 'Made')?.value).toBe('from the last frame of lighthouse-dusk.mp4')
    video.act('again', clipPath('lighthouse-dusk-4.mp4'))
    expect(video.face().form).toMatchObject({ continuing: true, continues: 'lighthouse-dusk.mp4', canAddImage: false, image: '' })
  })

  test('Install while a bootstrap runs starts nothing: it names the bootstrap, and the consent stays for once it has finished', async () => {
    const { video, clock, registry, said, sheets } = await place()
    registry.register('job', 'apogee bootstrap', async () => {})
    video.act('again', rainRecord())
    video.generate()
    await tick(clock, 3)
    // The refusal is said, not asked, while the bootstrap installs.
    expect(said.at(-1)).toBe('apogee bootstrap is already running: generate the clip again once it has finished')
    expect(sheets).toEqual([])
    // Asked from Settings' Install… while it runs: the sheet opens, and Install starts nothing.
    video.ask({ arguments: installVideo, said: 'Video · runtime: not set up' })
    video.install()
    expect(said.at(-1)).toBe('apogee bootstrap is already running: press Install again once it has finished')
    expect(video.face().consent.shown).toBe(true)
    expect(registry.children('job').map((job) => job.label)).toEqual(['apogee bootstrap'])
  })

  test('the player toggles, seeks and stops at the clip’s end, off the clock', async () => {
    const { video, clock } = await place()
    video.select(clipPath(clips.final))
    expect(video.face().clip?.player).toMatchObject({ playing: false, time: '0:00.0 / 0:05.0', label: '5 s · 121 frames' })
    video.togglePlayer()
    await tick(clock, 5)
    expect(video.face().clip?.player).toMatchObject({ playing: true, time: '0:02.0 / 0:05.0', label: 'frame 49 of 121', toggleLabel: 'Pause' })
    video.togglePlayer()
    await tick(clock, 5)
    expect(video.face().clip?.player.time).toBe('0:02.0 / 0:05.0')
    video.playback.seek(`clip:${clipPath(clips.final)}`, 4)
    await tick(clock, 10)
    expect(video.face().clip?.player).toMatchObject({ playing: false, time: '0:05.0 / 0:05.0', pct: 100 })
  })

  test('a record whose clip is gone is passed over, and said; a folder with none says how a clip is made', async () => {
    const { video } = await place({ folders: videoFoldersWithoutClip(clips.harbor) })
    expect(video.face().skipped).toEqual([`${clips.harbor}.json: its file is gone: ${clipPath(clips.harbor)}`])
    const empty = await place({ folders: {} })
    expect(empty.video.face().empty).toBe('No clips yet. A clip is made from a prompt or a seed image, with its record beside it in ~/Movies/Apogee.')
    expect(empty.video.face().clip.shown).toBe(false)
  })

  test('with no apogee the clips still list -- they are files -- and nothing generates', async () => {
    const { video } = await place({ apogee: 'missing' })
    expect(video.clips).toHaveLength(5)
    expect(video.face().pipelineLine).toBe('no default pipeline')
    expect(video.face().form).toMatchObject({ canGenerate: false, goNote: 'apogee is not installed' })
  })

  test('the clip actions set the form up and say so: the final, run again, a new seed, the continuation', async () => {
    const { video, said } = await place()
    video.act('extend', clipPath(clips.final))
    expect(video.face().form).toMatchObject({ heading: 'Continue lighthouse-dusk.mp4', lockFrame: true, continuing: true, told: 'extending /Users/someone/Movies/Apogee/lighthouse-dusk.mp4 from its last frame; from its record: model Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6, the prompt, 1280x704, 24 fps' })
    expect(said.at(-1)).toBe('Set up on the right: continuing lighthouse-dusk.mp4 from its last frame.')
    video.act('reroll', clipPath(clips.final))
    expect(video.face().form.seedMode).toBe('given')
    expect(video.face().form.command).toMatch(/^apogee video generate --from ~\/Movies\/Apogee\/lighthouse-dusk\.mp4\.json --seed \d+ -o ~\/Movies\/Apogee\/lighthouse-dusk-3\.mp4$/)
    video.act('finder', clipPath(clips.final))
    expect(said.at(-1)).toBe('Revealed ~/Movies/Apogee/lighthouse-dusk.mp4 in Finder (prototype)')
    video.startFresh()
    expect(video.face().form.heading).toBe('New clip')
  })
})
