import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import type { Folders } from '../../../source/infrastructure/process/StandInFolders'
import voiceList from '@recorded/business/reads/Fixtures/compose-voice-list.json?raw'
import { composeFiles, composeWorld, heatPumpFrame, moviesFolder, passedOver, projects, recordedBrief, recordedEdit, tidesFiles } from '../../../source/presentation/compose/Previews/ComposeWorlds'
import { ComposeViewModel } from '../../../source/presentation/compose/ViewModels/ComposeViewModel'
import { MediaPreferences } from '../../../source/presentation/styles/Media/MediaPreferences'
import { Playback } from '../../../source/presentation/styles/Media/Playback'

/** Lets the replayed jobs' updates land: each `for await` takes a turn of the event loop. */
const settle = async (rounds = 4) => {
  for (let round = 0; round < rounds; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The compositions over the world, read, on a clock the test moves; what they said. */
async function place(options: { readonly rules?: Rules; readonly folders?: Folders; readonly apogee?: ApogeeMode; readonly voice?: string; readonly captions?: boolean } = {}) {
  const apogee = options.apogee ?? 'installed'
  const child = new StandInChild({ rules: options.rules ?? composeWorld(), apogee, folders: options.folders ?? { [moviesFolder]: [...composeFiles(), ...tidesFiles()] } }, { configTypoRefusal: agentsListBadConfig })
  const clock = new Clock({})
  const registry = new ChildRegistry()
  const media = new MediaPreferences(options.voice ?? 'af_heart', options.captions ?? true)
  const compose = new ComposeViewModel(new ReadRunner(child), new JobStarter(child, clock, registry), child.folders, clock, new Playback(() => clock.ticks), media)
  const said: string[] = []
  compose.say = (text) => said.push(text)
  compose.folders = () => [moviesFolder]
  compose.passedOver = passedOver
  compose.startingBrief = recordedBrief
  compose.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  await compose.appeared()
  return { compose, child, clock, registry, said, media }
}

/** Ticks the clock `n` times, letting each tick's lines land. */
async function tick(clock: Clock, n: number) {
  for (let at = 0; at < n; at += 1) {
    clock.advance(1)
    await settle(2)
  }
  await settle()
}

/** The project page's face. */
const page = (compose: ComposeViewModel) => compose.face().page

describe('the compositions: the projects the CLI keeps beside its videos (design 1l, the Mac’s ComposeViewModel)', () => {
  test('the library: the old four as projects in ~/Movies/Apogee, newest first, each in its status; the passed-over files listed in the scan’s words', async () => {
    const { compose } = await place({ folders: { [moviesFolder]: composeFiles() } })
    const face = compose.face()
    expect(face.list.map((row) => [row.title, row.file, row.tag, row.status])).toEqual([
      ['How a heat pump works', 'heat-pump.mp4', '', 'rendered · 34 s · Today 09:40'],
      ['Night market teaser', 'night-market-2.mp4', 'EDIT 1', 'rendered · 18 s · Yesterday 21:30'],
      ['Tide pools for kids', 'tide-pools.mp4', '', 'stopped at scene 5 · resumable'],
      ['Night market teaser', 'night-market.mp4', '', 'rendered · 18 s · Yesterday 20:05'],
    ])
    expect(face.skipped).toEqual(['cut-short.mp4.json: not a JSON document', 'moved.mp4.json: its file is gone: /Users/someone/Elsewhere/moved.mp4.json'])
    expect(face.list[0]?.current).toBe(true)
    expect(face.draft.shown).toBe(false)
    expect(face.paneLabel).toBe('Make changes')
  })

  test('the heat pump, made: its page, the player paused at 9.6 s on its caption, its timeline, scenes, files, settings and chain; the edit pane open', async () => {
    const { compose } = await place()
    compose.select(projects.heatPump)
    compose.hold(heatPumpFrame)
    const p = page(compose)
    expect([p.title, p.status, p.meta, p.showPlayer, p.showProgress]).toEqual(['How a heat pump works', 'rendered', '6 scenes · about 34 s · narrated by af_heart · captions beside it · over soft-piano.m4a', true, false])
    expect([p.player.time, p.player.caption, p.player.badge, p.player.dims, p.player.right]).toEqual(['0:09.6 / 0:33.5', 'A heat pump gathers it with a refrigerant that runs colder than the air itself.', 'captions from heat-pump.srt', '1280×704 · 24 fps', '6 scenes'])
    expect(p.player.marks).toEqual([16.4, 32.8, 47.8, 64.2, 80.6])
    expect(p.timeline.pictures.map((block) => block.len)).toEqual(['6 s', '6 s', '5.5 s', '6 s', '6 s', '6.5 s'])
    expect([p.timeline.legend, p.timeline.end, p.timeline.bed]).toEqual(['soft-piano.m4a, ducked under the narration', '0:33.5', true])
    expect(p.scenes[0]).toMatchObject({ n: '1', voiceText: 'narration 3.62 s, measured', clipText: 'clip made', seedText: 'seed 1840277', secs: '6 s', canRedo: true })
    expect(p.files.map((file) => [file.path, file.what])).toEqual([
      ['heat-pump.mp4', 'the video'],
      ['heat-pump.mp4.json', 'the project'],
      ['heat-pump.scenes/', '6 scenes, each with its records'],
      ['heat-pump.srt', 'captions'],
    ])
    expect(p.facts.map((fact) => `${fact.label}: ${fact.value}`)).toEqual(['Voice: Kokoro-82M · af_heart', 'Pipeline: Wan2.2-TI2V-5B · MLX', 'Music: soft-piano.m4a', 'Assets: none', 'Captions: kept beside it as heat-pump.srt', 'Planner: root · on this Mac · 1 attempt', 'Made: Today 09:40'])
    expect(p.chain.map((link) => [link.label, link.how])).toEqual([['heat-pump.mp4', 'planned from the brief by root · Today 09:40']])
    const edit = compose.face().edit
    // The edit pass is the chat role's, as the CLI's video compose --edit resolves it, not the project's planner (root).
    expect([edit.locked, edit.textNote, edit.redo.text, edit.cantPreview, edit.previewNote]).toEqual([false, 'One pass by fastroot: it reads the brief, the numbered scenes and your words, and answers with the whole plan.', 'Tick Re-roll on a scene for a new take of it, everything else held.', true, 'Without an instruction there’s no model call.'])
    expect(edit.command).toBe('apogee video compose --from ~/Movies/Apogee/heat-pump.mp4 --dry-run -o ~/Movies/Apogee/heat-pump-edit.mp4')
  })

  test('an edit’s page: its changes from its own document, read against the project it came from, and its chain back to it', async () => {
    const { compose } = await place()
    compose.select(projects.nightMarket2)
    const p = page(compose)
    expect([p.tag, p.isEdit]).toEqual(['EDIT 1', true])
    expect(p.changes.map((row) => [row.n, row.text])).toEqual([
      ['1', 'kept as it is'],
      ['2', 'changed: prompt'],
      ['3', 'kept as it is'],
      ['4', 'changed: seed'],
    ])
    expect(p.chain.map((link) => [link.label, link.how, link.link])).toEqual([
      ['night-market-2.mp4', 'edited from night-market.mp4.json: “make the second scene the lantern stall, with someone lighting the lanterns” · re-rolled scene 4 · Yesterday 21:30', false],
      ['night-market.mp4', 'planned from the brief by root · Yesterday 20:05', true],
    ])
    expect(p.scenes[2]).toMatchObject({ shows: 'takes stall-0412.mov as it is', clipText: 'clip taken as it is', seedText: '', canRedo: false })
  })

  test('the stopped tide: its phases, the stop note from what its project records, Resume offered and the edit pane locked', async () => {
    const { compose } = await place()
    compose.select(projects.tidePools)
    const p = page(compose)
    expect([p.status, p.stopped, p.rendering, p.showPlayer, p.showProgress]).toEqual(['stopped', true, false, false, true])
    expect(p.phases).toEqual([
      { label: 'Speak every narration', status: 'done', detail: '7 of 7 spoken', bar: 100 },
      { label: 'Render every clip', status: 'stopped', detail: '4 of 7 made', bar: 57 },
      { label: 'Assemble', status: 'pending', detail: '0.5 s crossfades · narration · captions beside', bar: undefined },
    ])
    expect(p.stopNote).toBe('Stopped at scene 5’s clip. Every finished scene is kept, and resuming renders only what’s missing. The partial clip isn’t kept: scene 5 renders again with its seed held.')
    expect(p.scenes.map((scene) => scene.clipText)).toEqual(['clip made', 'clip made', 'clip made', 'clip made', 'clip stopped; renders again, seed held', 'clip not made yet', 'clip not made yet'])
    const edit = compose.face().edit
    expect([edit.locked, edit.lockedText, edit.cantPreview]).toEqual([true, 'Resume it first: a stopped composition picks up where it stopped, and then it can be changed.', true])
    // Planned by claude (billed), edited by the chat role: no bill named.
    expect(edit.textNote).toBe('One pass by fastroot: it reads the brief, the numbered scenes and your words, and answers with the whole plan.')
  })
})

describe('a new composition: planned first, then rendered (the old New composition, the Mac’s compose)', () => {
  test('a plan goes from planning to planned and says nothing is rendered yet: the recorded brief’s plan, its scenes and what rendering them takes', async () => {
    const { compose, clock, said, child } = await place()
    compose.newComposition()
    let fresh = compose.face().fresh
    expect(compose.face().isNew).toBe(true)
    expect([fresh.brief, fresh.count, fresh.planLabel, fresh.out, fresh.voice, fresh.hasVoiceModel, fresh.voiceModel]).toEqual([recordedBrief, '22 / 2,000', 'Plan', 'the-tides-twice-a-day.mp4', 'af_heart', true, 'Kokoro-82M/voice/5f3a2c91d0e4'])
    expect(fresh.captionsLine).toBe('Captions will be kept beside the video as the-tides-twice-a-day.srt, not burned in: no subtitles filter (this ffmpeg was built without libass).')
    expect(fresh.captionsRemedy).toBe('To burn them in: brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full')
    expect(fresh.command).toBe('apogee video compose "the tides, twice a day" --voice-model Kokoro-82M/voice/5f3a2c91d0e4 -o ~/Movies/Apogee/the-tides-twice-a-day.mp4')
    compose.compose(true)
    fresh = compose.face().fresh
    expect([fresh.planning, fresh.planningText, compose.face().draft.meta]).toEqual([true, 'planning the scenes with fastroot · one structured call, thinking off', 'planning…'])
    await tick(clock, 12)
    fresh = compose.face().fresh
    expect([fresh.planned, fresh.title, fresh.lengthText, fresh.planLabel, compose.face().draft.meta]).toEqual([true, 'Tides', '2 scenes · about 6 s of video', 'Plan again', 'planned · nothing rendered'])
    expect(fresh.scenes).toEqual([
      { n: '1', narration: 'The sea breathes.', shows: 'shows: a shore at dawn', secs: '3 s' },
      { n: '2', narration: 'Twice a day.', shows: 'shows: the moon over water', secs: '3 s' },
    ])
    expect(fresh.spend.map((line) => `${line.label} ${line.value}`)).toEqual([
      'PLAN planned by planner · 1 attempt',
      'SPEND 2 narration runs (faster than real time), then 2 clip runs at 1280×704, 50 steps. Each clip takes minutes on Apple silicon.',
      'CAPTIONS kept beside the video as the-tides-twice-a-day.srt; this ffmpeg has no libass to burn them',
      'MUSIC none',
    ])
    expect(said).toEqual(['Planned the-tides-twice-a-day.mp4: nothing is rendered yet -- Render it, on its page, renders the plan'])
    expect(child.unanswered).toEqual([])
    // A changed brief drops the plan, as the old one does.
    compose.editDraft({ brief: 'the tides, twice a day, at night' })
    expect(compose.face().fresh.planned).toBe(false)
  })

  test('a project renders scene by scene, read from the project the composer saves after every step: spoken, rendered, assembled, then ready', async () => {
    const { compose, clock, said, registry } = await place()
    compose.newComposition()
    compose.compose(true)
    await tick(clock, 12)
    compose.compose()
    expect(compose.selected).toBe(projects.fresh)
    expect(compose.draft).toBeUndefined()
    expect(registry.children('job').map((job) => [job.label, job.arguments?.join(' ')])).toEqual([['apogee video compose the tides, twice a day', 'video compose -o /Users/someone/Movies/Apogee/the-tides-twice-a-day.mp4 --voice-model Kokoro-82M/voice/5f3a2c91d0e4 --output-format stream-json -- the tides, twice a day']])
    await tick(clock, 8)
    let p = page(compose)
    expect([p.status, p.rendering, p.phases[0]?.status, p.phases[0]?.detail]).toEqual(['speaking scene 2', true, 'running', '1 of 2 spoken'])
    expect(compose.face().list[0]).toMatchObject({ file: 'the-tides-twice-a-day.mp4', status: 'rendering', rendering: true })
    await tick(clock, 6)
    p = page(compose)
    expect([p.status, p.phases[0]?.status, p.phases[1]?.status, p.phases[1]?.detail]).toEqual(['rendering scene 1 · step 20 of 50', 'done', 'running', '0 of 2 made · scene 1, step 20 of 50'])
    expect(p.scenes[0]).toMatchObject({ voiceText: 'narration 1.42 s, measured', clipText: 'clip · step 20 of 50' })
    expect(compose.face().edit).toMatchObject({ locked: true, lockedText: 'It’s still rendering. Changes open once it’s done.' })
    await tick(clock, 20)
    p = page(compose)
    expect([p.status, p.showPlayer, p.showProgress]).toEqual(['rendered', true, false])
    expect(said.at(-1)).toBe('the-tides-twice-a-day.mp4 is ready, its captions beside it as an .srt')
    expect(registry.children('job')).toEqual([])
  })

  test('a brief or assets changed while it plans: the plan that lands is the earlier words’, dropped and said, so Plan is offered again and Render never runs a brief nobody planned', async () => {
    for (const change of [(c: ComposeViewModel) => c.editDraft({ brief: 'a lighthouse keeper at night' }), (c: ComposeViewModel) => c.addAssets()]) {
      const { compose, clock, said } = await place()
      compose.newComposition()
      compose.compose(true)
      await tick(clock, 3)
      change(compose)
      expect(compose.face().fresh.planning).toBe(true)
      await tick(clock, 12)
      const fresh = compose.face().fresh
      expect([fresh.planned, fresh.planning, fresh.planLabel, fresh.title, compose.face().draft.meta]).toEqual([false, false, 'Plan', '', 'a brief, not planned yet'])
      expect(said).toEqual(['The brief changed while it was planned: plan it again'])
    }
  })

  test('Render shows the composition at once, rendering, its row first and current -- the project beside its video from the start -- and a Stop before its first line keeps it, stopped and resumable', async () => {
    const { compose, clock } = await place()
    compose.newComposition()
    compose.compose(true)
    await tick(clock, 12)
    compose.compose()
    expect([page(compose).shown, page(compose).status, page(compose).title]).toEqual([true, 'rendering', 'Tides'])
    expect(compose.face().list[0]).toMatchObject({ file: 'the-tides-twice-a-day.mp4', status: 'rendering', current: true })
    await compose.stop()
    await settle()
    expect([compose.selected, page(compose).status, compose.selectedProject?.status, page(compose).phases[0]?.detail]).toEqual([projects.fresh, 'stopped', 'cancelled', '0 of 2 spoken'])
    expect(compose.face().list[0]).toMatchObject({ file: 'the-tides-twice-a-day.mp4', status: 'stopped at scene 1 · resumable', current: true })
    compose.resume(compose.selectedProject!)
    await tick(clock, 40)
    expect(page(compose).status).toBe('rendered')
  })

  test('a store with one voice model names none: the CLI takes it as it is, and the form offers no picker', async () => {
    const world = composeWorld()
    const { compose } = await place({ rules: { ...world, reads: [...world.reads, { arguments: ['models', 'list'], answer: { stdout: voiceList } }] } })
    compose.newComposition()
    const fresh = compose.face().fresh
    expect([fresh.hasVoiceModel, fresh.voiceModel]).toEqual([false, ''])
    expect(fresh.command).toBe('apogee video compose "the tides, twice a day" -o ~/Movies/Apogee/the-tides-twice-a-day.mp4')
  })

  test('Stop on the new render keeps what it made, and Resume runs its own command again, its voice model named, rendering only the rest', async () => {
    const { compose, clock, child } = await place()
    compose.newComposition()
    compose.compose(true)
    await tick(clock, 12)
    compose.compose()
    await tick(clock, 7)
    await compose.stop()
    await settle()
    expect(page(compose)).toMatchObject({ status: 'stopped', stopNote: 'Stopped. Every finished scene is kept, and resuming renders only what’s missing.' })
    expect(page(compose).phases[0]?.detail).toBe('1 of 2 spoken')
    compose.resume(compose.selectedProject!)
    const renders = child.calls.filter((call) => call.kind === 'jobs' && !call.arguments.includes('--dry-run'))
    expect(renders.map((call) => call.arguments.join(' '))).toEqual([0, 1].map(() => 'video compose -o /Users/someone/Movies/Apogee/the-tides-twice-a-day.mp4 --voice-model Kokoro-82M/voice/5f3a2c91d0e4 --output-format stream-json -- the tides, twice a day'))
    // Its first line, then on from the scene it had not spoken: scene 1's narration is never spoken again.
    await tick(clock, 3)
    expect([page(compose).status, page(compose).phases[0]?.detail]).toEqual(['speaking scene 2', '1 of 2 spoken'])
    await tick(clock, 30)
    expect(page(compose).status).toBe('rendered')
  })

  test('the chosen voice: a new composition starts from the window’s preference; an open one keeps its own when Settings changes it, and takes Voice’s Use this voice for compositions', async () => {
    const { compose, media } = await place({ voice: 'bm_george' })
    compose.newComposition()
    expect(compose.face().fresh.voice).toBe('bm_george')
    // Settings' Voice for new speech (the old 6256) sets the default alone: the open draft keeps its voice.
    media.chooseVoice('bf_emma')
    expect(compose.face().fresh.voice).toBe('bm_george')
    expect(compose.face().fresh.command).toContain('--voice bm_george')
    // Voice's Use this voice for compositions (the old 7692) sets the default and the open draft's.
    media.useForCompositions('am_adam')
    expect(compose.face().fresh.voice).toBe('am_adam')
    expect(compose.face().fresh.command).toContain('--voice am_adam')
  })

  test('captions: a new composition starts from the window’s preference -- off, the box unticked, --no-captions on its command and on the run it starts', async () => {
    const { compose, child } = await place({ captions: false })
    compose.newComposition()
    const fresh = compose.face().fresh
    expect([fresh.captions, fresh.captionsLine, fresh.captionsRemedy]).toEqual([false, '', ''])
    expect(fresh.command).toBe('apogee video compose "the tides, twice a day" --no-captions --voice-model Kokoro-82M/voice/5f3a2c91d0e4 -o ~/Movies/Apogee/the-tides-twice-a-day.mp4')
    compose.compose(true)
    expect(child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments.includes('--no-captions'))).toEqual([true])
  })
})

describe('Stop and Resume: one render at a time, the finished scenes kept (the old project page)', () => {
  test('Stop keeps the finished scenes and says where it stopped; Resume renders only the rest, never a scene already made', async () => {
    const { compose, clock, child } = await place()
    compose.select(projects.tidePools)
    compose.resume(compose.selectedProject!)
    expect(page(compose).status).toBe('rendering')
    await tick(clock, 16)
    expect([page(compose).status, page(compose).phases[1]?.detail]).toEqual(['rendering scene 6 · step 20 of 50', '5 of 7 made · scene 6, step 20 of 50'])
    await compose.stop()
    await settle()
    let p = page(compose)
    expect([p.status, p.stopped, p.phases[1]?.detail, p.phases[1]?.status]).toEqual(['stopped', true, '5 of 7 made', 'stopped'])
    expect(p.stopNote).toBe('Stopped at scene 6’s clip, step 20 of 50. Every finished scene is kept, and resuming renders only what’s missing. The partial clip isn’t kept: scene 6 renders again with its seed held.')
    expect(compose.face().list.find((row) => row.file === 'tide-pools.mp4')?.status).toBe('stopped at scene 6 · resumable')
    compose.resume(compose.selectedProject!)
    expect(page(compose).stopNote).toBe('')
    // Its first line, then on from scene 6: the five finished scenes are never made again.
    await tick(clock, 3)
    expect([page(compose).status, page(compose).phases[1]?.detail]).toEqual(['rendering scene 6 · step 1 of 50', '5 of 7 made · scene 6, step 1 of 50'])
    await tick(clock, 37)
    p = page(compose)
    expect([p.status, p.phases.length === 3 ? 'phases' : '', compose.selectedProject?.status]).toEqual(['rendered', 'phases', 'done'])
    const resumes = child.calls.filter((call) => call.arguments[1] === 'compose')
    expect(resumes).toHaveLength(2)
    expect(child.unanswered).toEqual([])
  })

  test('a second render is refused in the prototype’s words: Resume while one renders, and an edit’s Render', async () => {
    const { compose, clock, said, child } = await place()
    compose.select(projects.tidePools)
    compose.resume(compose.selectedProject!)
    compose.select(projects.tides)
    compose.editForm({ instruction: recordedEdit })
    const previewing = compose.previewEdit()
    await tick(clock, 5)
    await previewing
    expect(compose.face().edit.shown).toBe(true)
    compose.renderEdit()
    compose.select(projects.tidePools)
    compose.resume(compose.selectedProject!)
    expect(said).toEqual(['One composition renders at a time. Wait for the other, or stop it.', 'One composition renders at a time. Stop the other first.'])
    // Refused, so nothing more ran: the tide's one render, and the preview on paper.
    expect(child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments.includes('--dry-run'))).toEqual([false, true])
  })
})

describe('an edit: previewed on paper, then rendered as a new video (the old Make changes, the Mac’s previewEdit and renderEdit)', () => {
  /** Tides, made, its edit typed and previewed: the recorded dry run. */
  async function previewed() {
    const world = await place()
    world.compose.select(projects.tides)
    world.compose.editForm({ instruction: recordedEdit })
    const previewing = world.compose.previewEdit()
    expect(world.compose.face().edit).toMatchObject({ previewing: true, previewingText: 'Planning on paper -- at most 5 minutes' })
    await tick(world.clock, 5)
    await previewing
    return world
  }

  test('the preview: the recorded dry run’s diff in the CLI’s words, the spend, and Render saying it plans again; the command without --dry-run', async () => {
    const { compose } = await previewed()
    const edit = compose.face().edit
    expect([edit.shown, edit.head]).toEqual([true, 'the edit: 2 scenes, were 2'])
    expect(edit.diff.map((row) => `${row.n} ${row.text}`)).toEqual(['1. scene 1, untouched', '2. scene 2; its prompt changed'])
    expect(edit.spend).toBe('Spend: 1 clip run at 1280×704, 50 steps, minutes each on Apple silicon; 1 clip already made.')
    expect(edit.textNote).toBe('One pass by planner: it reads the brief, the numbered scenes and your words, and answers with the whole plan.')
    expect(edit.replans.text).toBe('Render plans the edit again with planner: what renders can differ from this preview, and the planner is called a second time.')
    expect(edit.renderTitle).toBe('Plans the edit again with planner, then renders it: it can differ from this preview')
    expect(edit.command).toBe('apogee video compose --from ~/Movies/Apogee/tides.mp4 --edit "make the second scene the moon" -o ~/Movies/Apogee/tides-edit.mp4')
  })

  test('a re-roll previews by the CLI’s recorded dry runs: scene 2 alone a new seed, and scene 1 re-rolled beside the recorded instruction', async () => {
    const { compose, clock, child } = await place()
    compose.select(projects.tides)
    compose.toggleRedo(2)
    let previewing = compose.previewEdit()
    await tick(clock, 5)
    await previewing
    let edit = compose.face().edit
    expect([edit.shown, edit.failure]).toEqual([true, ''])
    expect(edit.diff.map((row) => `${row.n} ${row.text}`)).toEqual(['1. scene 1, untouched', '2. scene 2; a new seed'])
    expect(edit.renderTitle).toBe('Renders the scenes chosen again on fresh seeds; nothing is planned')
    compose.edit(compose.selectedProject!)
    compose.editForm({ instruction: recordedEdit })
    compose.toggleRedo(1)
    previewing = compose.previewEdit()
    await tick(clock, 5)
    await previewing
    edit = compose.face().edit
    expect(edit.diff.map((row) => `${row.n} ${row.text}`)).toEqual(['1. scene 1; a new seed', '2. scene 2; its prompt changed'])
    expect(child.unanswered).toEqual([])
  })

  test('stopping a preview says nothing was changed, and draws none', async () => {
    const { compose, said } = await place()
    compose.select(projects.tides)
    compose.editForm({ instruction: recordedEdit })
    const previewing = compose.previewEdit()
    await settle()
    await compose.stopPreview()
    await previewing
    expect(said).toEqual(['Preview stopped: nothing was changed'])
    expect(compose.face().edit).toMatchObject({ shown: false, previewing: false })
  })

  test('an edit form changed after its preview drops the preview: the words, a re-roll, a setting', async () => {
    for (const change of [(c: ComposeViewModel) => c.editForm({ instruction: `${recordedEdit}, at night` }), (c: ComposeViewModel) => c.toggleRedo(1), (c: ComposeViewModel) => c.editForm({ voice: 'bf_emma' })]) {
      const { compose } = await previewed()
      change(compose)
      expect(compose.face().edit).toMatchObject({ shown: false, diff: [], previewNote: expect.any(String) })
      expect(compose.preview).toBeUndefined()
    }
  })

  test('a clip the edit carried that is now too short for its narration renders again: Render every clip says 1 of 2 made while it does, and nothing assembles early', async () => {
    const { compose, clock } = await previewed()
    compose.renderEdit()
    await tick(clock, 12)
    const p = page(compose)
    // The composer carried scene 1's clip (its narration alone changed), then found it 3.00 s against the 3.41 s its narration now needs.
    expect(compose.selectedProject?.scenes[0]).toMatchObject({ clipFile: `${moviesFolder}/tides-edit.scenes/s01.mp4`, clipSeconds: 3, voiceSeconds: 2.41 })
    expect([p.status, p.phases.map((phase) => `${phase.status}: ${phase.detail}`)]).toEqual(['rendering scene 1 · step 20 of 50', ['done: 2 of 2 spoken', 'running: 1 of 2 made · scene 1, step 20 of 50', 'pending: 0.5 s crossfades · narration · captions beside']])
    // Its length the one it renders at (the CLI's scene_length, 3.41 s), not the carried clip's 3 s.
    expect(p.scenes[0]).toMatchObject({ clipText: 'clip · step 20 of 50', secs: '3.41 s' })
    expect(p.timeline.pictures.map((block) => block.made)).toEqual([false, true])
  })

  test('an edit form changed while its preview plans drops that preview: the words, a re-roll, a setting', async () => {
    for (const change of [(c: ComposeViewModel) => c.editForm({ instruction: `${recordedEdit}, at night` }), (c: ComposeViewModel) => c.toggleRedo(1), (c: ComposeViewModel) => c.editForm({ voice: 'bf_emma' })]) {
      const { compose, clock } = await place()
      compose.select(projects.tides)
      compose.editForm({ instruction: recordedEdit })
      const previewing = compose.previewEdit()
      change(compose)
      await tick(clock, 5)
      await previewing
      expect(compose.face().edit).toMatchObject({ shown: false, previewing: false })
      expect(compose.preview).toBeUndefined()
    }
  })

  test('rendered as a new video: the render plans again, its own changes differ from the preview, and the page says so above them', async () => {
    const { compose, clock, said } = await previewed()
    compose.renderEdit()
    expect(compose.selected).toBe(projects.tidesEdit)
    // Shown at once, rendering, its row first, as the old adds it.
    expect([page(compose).shown, page(compose).status, page(compose).tag]).toEqual([true, 'rendering', 'EDIT 1'])
    expect(compose.face().list[0]).toMatchObject({ file: 'tides-edit.mp4', status: 'rendering', current: true })
    await tick(clock, 25)
    const p = page(compose)
    expect([p.title, p.tag, p.status, p.differs]).toEqual(['Tides', 'EDIT 1', 'rendered', 'The render planned again, and its changes differ from the preview'])
    // Each scene as long as its clip measured: scene 1's narration grew, so its clip did.
    expect([p.scenes[0]?.secs, p.scenes[1]?.secs, p.player.time]).toEqual(['3.4 s', '3 s', '0:00.0 / 0:05.9'])
    expect(p.changes.map((row) => `${row.n} ${row.text}`)).toEqual(['1 changed: narration', '2 kept as it is'])
    expect(said.at(-1)).toBe('tides-edit.mp4 is ready, its captions beside it as an .srt')
    expect(compose.face().list.map((row) => row.file).slice(0, 2)).toEqual(['tides-edit.mp4', 'heat-pump.mp4'])
  })
})
