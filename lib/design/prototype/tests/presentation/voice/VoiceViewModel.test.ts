import { describe, expect, test } from 'vitest'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import modelsStoredInfoVoice from '@recorded/business/reads/Fixtures/models-stored-info-voice.json?raw'
import storeInfoKindVoice from '@recorded/business/reads/Fixtures/store-info-kind-voice.json?raw'
import modelsListVideo from '../../../fixtures/business/reads/models-list-video-handmade.json?raw'
import modelsListKinds from '../../../fixtures/business/reads/models-list-kinds-handmade.json?raw'
import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import type { Folders } from '../../../source/infrastructure/process/StandInFolders'
import { MediaPreferences } from '../../../source/presentation/styles/Media/MediaPreferences'
import { Playback } from '../../../source/presentation/styles/Media/Playback'
import { musicFolder, sampleText, sayArguments, secondSentence, speeches, voiceFolders, voiceFoldersWithUntimedRecord, voiceModel, voiceWorld } from '../../../source/presentation/voice/Previews/VoiceWorlds'
import { VoiceViewModel } from '../../../source/presentation/voice/ViewModels/VoiceViewModel'

/** Lets the replayed jobs' updates land: each `for await` takes a turn of the event loop. */
const settle = async (rounds = 4) => {
  for (let round = 0; round < rounds; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The place over the Voice world, read, on a clock the test moves; what it said, and the shared preferences and job list. */
async function place(options: { readonly rules?: Rules; readonly folders?: Folders; readonly apogee?: ApogeeMode; readonly media?: MediaPreferences } = {}) {
  const apogee = options.apogee ?? 'installed'
  const child = new StandInChild({ rules: options.rules ?? voiceWorld(), apogee, folders: options.folders ?? voiceFolders() }, { configTypoRefusal: agentsListBadConfig })
  const clock = new Clock({})
  const registry = new ChildRegistry()
  const media = options.media ?? new MediaPreferences('af_heart')
  const voice = new VoiceViewModel(new ReadRunner(child), new JobStarter(child, clock, registry), child.folders, clock, new Playback(() => clock.ticks), media)
  const said: string[] = []
  const opened: string[] = []
  voice.say = (text) => said.push(text)
  voice.openModels = (handle) => opened.push(handle)
  voice.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  await voice.appeared()
  voice.setText(sampleText)
  return { voice, child, clock, registry, media, said, opened }
}

/** Ticks the clock `n` times, letting each tick's lines land. */
async function tick(clock: Clock, n: number) {
  for (let at = 0; at < n; at += 1) {
    clock.advance(1)
    await settle(2)
  }
  await settle()
}

describe('the Voice place: its speeches, the speak card, the selected speech, a run (design 1m, the Mac’s VoiceViewModel)', () => {
  test('the library: the two recorded speeches in ~/Music/Apogee, newest first (their tie in the scan’s order), the newest shown; the header names the store’s voice model and its engine, and opens it in the store', async () => {
    const { voice, opened } = await place()
    expect(voice.speeches.map((speech) => speech.name)).toEqual(['hello-mlx.wav', 'hello-pytorch.wav'])
    const face = voice.face()
    expect(face.modelLine).toBe('Kokoro-82M · PyTorch')
    expect(face.modelTitle).toBe('Show Kokoro-82M in the library')
    expect(face.list.map((row) => `${row.file} | ${row.text} | ${row.meta} | ${row.current}`)).toEqual([
      'hello-mlx.wav | Hello there. How are you? | af_heart · 1.0× · 0.1 s | true',
      'hello-pytorch.wav | Hello there. How are you? | af_heart · 1.0× · 0.1 s | false',
    ])
    expect(face.empty).toBe('')
    expect(face.speech.shown).toBe(true)
    voice.openModel()
    expect(opened).toEqual([voiceModel])
  })

  test('the speak card: the text and its count, the sentence line, the voices the model’s models info lists by language, the speed, Save as and the command in the prototype’s words', async () => {
    const { voice } = await place()
    const card = voice.face().card
    expect(card.count).toBe('118 / 4,000')
    expect(card.countColor).toBe('var(--text-tertiary)')
    expect(card.sentencesText).toBe('2 sentences · split where . ! or ? meets a space, never by a model')
    expect(card.groups.map((group) => `${group.title}: ${group.voices.map((v) => `${v.id}${v.hint === '' ? '' : ` ${v.hint}`}${v.current ? ' *' : ''}`).join(', ')}`)).toEqual(['AMERICAN ENGLISH · 2: af_heart default *, am_adam', 'BRITISH ENGLISH · 1: bf_emma'])
    expect(card.groups[0]?.voices[1]?.title).toBe('am_adam, a male voice')
    expect(card.speedText).toBe('1.0×')
    expect(card.speedChanged).toBe(false)
    expect(card.out).toBe('apogee-runs-every.wav')
    expect(card.command).toBe(`apogee voice say "${sampleText}" -o ~/Music/Apogee/apogee-runs-every.wav`)
    expect(card.cantSpeak).toBe(false)
    expect(card.note).toBe('Speaks on PyTorch, through Kokoro: the voice model as Kokoro publishes it. Nothing leaves this Mac.')
    expect(voice.plannedArguments).toEqual(sayArguments.filter((_word, index) => !(index >= 4 && index <= 5)))
    voice.pickVoice('bf_emma')
    voice.setSpeed(1.25)
    const changed = voice.face().card
    expect(changed.speedText).toBe('1.25×')
    expect(changed.speedChanged).toBe(true)
    expect(changed.command).toBe(`apogee voice say "${sampleText}" --voice bf_emma --speed 1.25 -o ~/Music/Apogee/apogee-runs-every.wav`)
    voice.resetSpeed()
    expect(voice.face().card.speedText).toBe('1.0×')
  })

  test('text over 4,000 characters shows the error with its count and disables Speak', async () => {
    const { voice } = await place()
    voice.setText('a'.repeat(4001))
    const card = voice.face().card
    expect(card.errors).toEqual(['The text is 4,000 characters at most; this is 4,001.'])
    expect(card.count).toBe('4,001 / 4,000')
    expect(card.countColor).toBe('var(--status-error)')
    expect(card.cantSpeak).toBe(true)
    voice.speak()
    expect(voice.face().card.speaking).toBe(false)
  })

  test('text of exactly 4,000 characters is within the cap: no error, the count in its plain color, Speak on', async () => {
    const { voice } = await place()
    voice.setText('a'.repeat(4000))
    const card = voice.face().card
    expect(card.errors).toEqual([])
    expect(card.count).toBe('4,000 / 4,000')
    expect(card.countColor).toBe('var(--text-tertiary)')
    expect(card.cantSpeak).toBe(false)
  })

  test('a planted output name that already exists shows "never writes over a file" and disables Speak; emptied, it asks for a name', async () => {
    const { voice } = await place()
    voice.setOut('hello-pytorch.wav')
    expect(voice.face().card.errors).toEqual(['~/Music/Apogee/hello-pytorch.wav exists. A speech never writes over a file; name another.'])
    expect(voice.face().card.cantSpeak).toBe(true)
    voice.setOut('hello-mlx.wav.json')
    expect(voice.face().card.errors[0]).toContain('never writes over a file')
    voice.setOut(' ')
    expect(voice.face().card.errors).toEqual(['Name the file to write.'])
    expect(voice.face().card.cantSpeak).toBe(true)
  })

  test('the selected speech: its meta, format, sentence table with STARTS and LASTS, what was measured, the record’s facts and its command, from speech-torch.json as written', async () => {
    const { voice } = await place()
    voice.select(speeches.torch)
    const speech = voice.face().speech
    expect(speech.file).toBe('hello-pytorch.wav')
    expect(speech.meta).toBe('af_heart · 1.0× · 0.1 s · Oct 22')
    expect(speech.actions.map((action) => `${action.label} ${action.kind}`)).toEqual(['Use these settings btn', 'Use this voice for compositions btn-g', 'Show in Finder btn-g'])
    expect(speech.format).toBe('16-bit mono WAV · 24 kHz · 0.1 s')
    expect(speech.sentences.map((row) => `${row.n} ${row.text} ${row.starts} ${row.lasts}`)).toEqual(['1 Hello there. 0.00 s 0.05 s', '2 How are you? 0.05 s 0.05 s'])
    expect(speech.measured).toBe('Each sentence’s seconds are counted from the samples the model wrote. The file’s 0.1 s are measured from its own WAV header.')
    expect(speech.record).toBe('hello-pytorch.wav.json')
    expect(speech.facts.map((fact) => `${fact.label}: ${fact.value}`)).toEqual([
      'Model: Kokoro-82M · voice/5f3a2c91d0e4',
      'Voice: af_heart · American English',
      'Speed: 1.0×',
      'Device: cpu',
      'Versions: PyTorch · kokoro 0.0-stub · torch 0.0-stub',
      'Segments: 1: a speech is one segment, its sentences timed inside it',
      'Took: 0.09 s',
    ])
    expect(speech.command).toBe('apogee voice say "Hello there. How are you?" -o ~/Music/Apogee/hello-pytorch.wav')
    expect(speech.bars).toHaveLength(96)
    expect(speech.player.time).toBe('0:00.0 / 0:00.1')
    // The MLX record: its model named, since the store's one voice model is not it; its device MLX's, its versions mlx-audio's.
    voice.select(speeches.mlx)
    const mlx = voice.face().speech
    expect(mlx.command).toBe('apogee voice say "Hello there. How are you?" -m Kokoro-82M/mlx/6f708192a3b4 -o ~/Music/Apogee/hello-mlx.wav')
    expect(mlx.facts.find((fact) => fact.label === 'Device')?.value).toBe('gpu')
    expect(mlx.facts.find((fact) => fact.label === 'Versions')?.value).toBe('MLX · mlx 0.31.2 · mlx-audio 0.0-audio-stub')
  })

  test('the lit sentence follows the record’s own offsets: a position of 0.06 s in speech-torch.json is sentence 2 -- lit while it plays, as the old row lights; paused there, the waveform says how far', async () => {
    const { voice } = await place()
    voice.select(speeches.torch)
    voice.hold(secondSentence)
    expect(voice.currentSentence).toBe(1)
    const paused = voice.face().speech
    expect(paused.sentences.map((row) => row.lit)).toEqual([false, false])
    expect(paused.player.toggleLabel).toBe('Play')
    expect(paused.player.time).toBe('0:00.1 / 0:00.1')
    // The bars before the position are drawn as played; none lit while paused.
    expect(paused.bars.filter((bar) => bar.color === 'var(--status-accent)')).toEqual([])
    expect(paused.bars.slice(0, 57).every((bar) => bar.color === 'var(--text-secondary)')).toBe(true)
    expect(paused.bars.slice(58).every((bar) => bar.color === 'var(--line-strong)')).toBe(true)
    // A sentence pressed plays from its start: the record's 0.05 s, lit.
    voice.seek(1)
    const playing = voice.face().speech
    expect(playing.player.toggleLabel).toBe('Pause')
    expect(playing.sentences.map((row) => row.lit)).toEqual([false, true])
    expect(playing.bars.some((bar) => bar.color === 'var(--status-accent)')).toBe(true)
    // Decoration only: each sentence's bars stand above the silence between them, scaled to a sentence this short.
    expect(playing.bars.filter((bar) => bar.height > 4).length).toBeGreaterThan(0)
  })

  test('Speak shows the running row and line -- on the shared job list, which the ⌘7 badge counts and the monitor names -- then selects the new record and raises the toast', async () => {
    const { voice, clock, registry, said } = await place()
    voice.speak()
    let face = voice.face()
    expect(face.list[0]).toMatchObject({ file: 'apogee-runs-every.wav', text: sampleText, meta: 'speaking · af_heart', running: true })
    // The card keeps the running command's name: its WAV lands only when the run finishes (the old `spNames`).
    expect(face.card.out).toBe('apogee-runs-every.wav')
    expect(face.card.command.endsWith('-o ~/Music/Apogee/apogee-runs-every.wav')).toBe(true)
    expect(face.card.speaking).toBe(true)
    expect(face.card.speakingText).toBe('speaking 2 sentences as af_heart')
    expect(face.card.cantSpeak).toBe(true)
    expect(registry.children('job').map((child) => child.arguments)).toEqual([sayArguments])
    expect(voice.speaking).toEqual({ arguments: sayArguments, name: 'Kokoro-82M · speaking', runtime: undefined })
    expect(voice.run?.status.case).toBe('running')
    await tick(clock, 3)
    face = voice.face()
    expect(face.card.speakingText).toBe('speaking 2 sentences as af_heart on the cpu')
    expect(voice.speaking?.runtime).toBe('cpu')
    await tick(clock, 3)
    expect(registry.children('job')).toEqual([])
    expect(voice.speaking).toBeUndefined()
    face = voice.face()
    expect(face.card.speaking).toBe(false)
    expect(voice.selected).toBe(speeches.said)
    expect(face.list.map((row) => `${row.file} ${row.current}`)).toEqual(['apogee-runs-every.wav true', 'hello-mlx.wav false', 'hello-pytorch.wav false'])
    expect(face.speech.meta).toBe('af_heart · 1.0× · 0.1 s · Today 12:00')
    expect(said).toEqual(['apogee-runs-every.wav is ready · 0.1 s, measured from its header'])
    // Its name is taken now: the card offers the next.
    expect(face.card.out).toBe('apogee-runs-every-2.wav')
  })

  test('Stop on the shared job list (the monitor’s) ends the speech as Ctrl-C does: nothing written, said in the CLI’s words', async () => {
    const { voice, clock, registry, said, child } = await place()
    voice.speak()
    await tick(clock, 2)
    const [running] = registry.children('job')
    if (!running) throw new Error('no run')
    await registry.stopChild(running.id)
    await settle()
    expect(voice.face().card.speaking).toBe(false)
    expect(voice.face().list.map((row) => row.file)).toEqual(['hello-mlx.wav', 'hello-pytorch.wav'])
    expect(child.folders.files(musicFolder).map((file) => file.name)).not.toContain('apogee-runs-every.wav')
    expect(said).toEqual(['cancelled -- nothing was written (/Users/someone/Music/Apogee/apogee-runs-every.wav untouched)'])
  })

  test('a speech nobody recorded fails in the stand-in’s words, said under Speak in the error color; the next Speak clears it', async () => {
    const { voice, clock } = await place()
    voice.setText('Something else.')
    voice.speak()
    await tick(clock, 2)
    const card = voice.face().card
    expect(card.speaking).toBe(false)
    expect(card.note).toContain('no recorded answer for apogee voice say')
    expect(card.noteColor).toBe('var(--status-error)')
    expect(voice.face().list).toHaveLength(2)
  })

  test('"Use this voice for compositions" writes the shared MediaPreferences that a new composition reads: the default and an open draft’s voice, never Settings’ chooseVoice alone', async () => {
    const media = new MediaPreferences('am_adam')
    const drafts: string[] = []
    media.onUseForCompositions((chosen) => drafts.push(chosen))
    const { voice, said } = await place({ media })
    voice.select(speeches.torch)
    voice.act('compositions')
    expect(media.voice).toBe('af_heart')
    expect(drafts).toEqual(['af_heart'])
    expect(said).toEqual(['af_heart is now the voice for new speech and compositions'])
  })

  test('"Use these settings" puts the record’s text, voice and speed in the card; Show in Finder says so; New speech empties the card from the chosen voice and shows no speech', async () => {
    const media = new MediaPreferences('bf_emma')
    const { voice, said } = await place({ media })
    expect(voice.face().card.groups[1]?.voices[0]?.current).toBe(true)
    voice.select(speeches.torch)
    voice.act('settings')
    expect(voice.text).toBe('Hello there. How are you?')
    expect(voice.voice).toBe('af_heart')
    expect(voice.face().card.out).toBe('hello-there-you.wav')
    voice.act('finder')
    expect(said).toEqual(['The text, voice and speed are in the editor above.', 'Revealed ~/Music/Apogee/hello-pytorch.wav in Finder (prototype)'])
    voice.newSpeech()
    const face = voice.face()
    expect([voice.text, voice.voice, voice.speed]).toEqual(['', 'bf_emma', 1])
    expect(face.speech.shown).toBe(false)
    expect(face.card.cantSpeak).toBe(true)
    expect(face.card.sentencesText).toBe('Sentences are split where . ! or ? meets a space, never by a model.')
    expect(face.card.command).toBe('apogee voice say "…" --voice bf_emma -o ~/Music/Apogee/untitled.wav')
    // Settings' choice changes only the default: the card keeps its own voice until New speech.
    voice.pickVoice('am_adam')
    media.chooseVoice('af_heart')
    expect(voice.voice).toBe('am_adam')
  })

  test('a planted record missing its segments is skipped and noted, never a throw; a record whose WAV is gone too', async () => {
    const { voice } = await place({ folders: voiceFoldersWithUntimedRecord() })
    expect(voice.speeches.map((speech) => speech.name)).toEqual(['hello-mlx.wav', 'hello-pytorch.wav'])
    expect(voice.face().skipped).toEqual(['no-timings.wav.json: a voice.speech record without its segments'])
  })

  test('nothing spoken yet: an empty folder says so; the store with two voice models names one, its engine the person’s', async () => {
    const rules: Rules = { reads: [{ arguments: ['models', 'list'], answer: { stdout: modelsListVideo } }, { arguments: ['models', 'info', voiceModel], answer: { stdout: modelsStoredInfoVoice } }, { arguments: ['models', 'info', 'Kokoro-82M/mlx/6f708192a3b4'], answer: { stdout: storeInfoKindVoice } }] }
    const { voice } = await place({ rules, folders: {} })
    const face = voice.face()
    expect(face.list).toEqual([])
    expect(face.empty).toBe('Nothing spoken yet.')
    expect(face.speech.shown).toBe(false)
    expect(face.modelLine).toBe('Kokoro-82M · PyTorch')
    expect(face.card.command).toBe(`apogee voice say "${sampleText}" -m Kokoro-82M/voice/5f3a2c91d0e4 --engine torch -o ~/Music/Apogee/apogee-runs-every.wav`)
    expect(face.card.note).toBe('Speaks on PyTorch, through Kokoro: the voice model as Kokoro publishes it. Kokoro-82M holds it and its MLX build, so the engine is yours: --engine torch. Nothing leaves this Mac.')
    expect(voice.plannedArguments).toEqual(['voice', 'say', '-o', speeches.said, '-m', voiceModel, '--engine', 'torch', '--', sampleText])
  })

  test('a store with no voice model says so in the CLI’s words; with no apogee, the speeches still list and Speak waits for it', async () => {
    const none = await place({ rules: { reads: [{ arguments: ['models', 'list'], answer: { stdout: modelsListKinds } }] } })
    expect(none.voice.face().modelLine).toBe('no voice model in the store')
    expect(none.voice.face().card.groups).toEqual([])
    expect(none.voice.face().card.note).toBe("no voice model in the store -- 'apogee models pull hexgrad/Kokoro-82M' stores the reference one")
    const missing = await place({ apogee: 'missing' })
    expect(missing.voice.speeches).toHaveLength(2)
    expect(missing.voice.face().modelLine).toBe('apogee is not installed')
    expect(missing.voice.face().card.cantSpeak).toBe(true)
    expect(missing.voice.face().card.note).toBe('apogee is not installed')
    const typo = await place({ apogee: 'config-typo' })
    expect(typo.voice.face().modelLine).toBe('the store could not be read')
    expect(typo.voice.face().card.note).toContain('apogee models: /Users/someone/.apogee/config/config.json: not valid JSON')
  })
})
