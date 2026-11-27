import { describe, expect, test } from 'vitest'
import { presetFor, presets } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel } from '../../../source/presentation/app/ViewModels/AppViewModel'
import { destinations, placeholderOf } from '../../../source/presentation/shell/ViewModels/Destination'
import { settingsPanes } from '../../../source/presentation/settings/ViewModels/SettingsViewModel'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'

/** Every path in `value` that reads `undefined`, `null` or `NaN`, or a string that says so: the old 30-tick check's walk. */
function unread(value: unknown, at: string, depth = 0): string[] {
  if (depth > 9) return []
  if (value === undefined || value === null) return [at]
  if (typeof value === 'number') return Number.isNaN(value) ? [at] : []
  // A note on the said-card is the CLI's own sentence, printed (GUI M22): its `and not null` is the CLI's word, never an unread value.
  if (typeof value === 'string') return /undefined|NaN|null/.test(value) && !/\.notes\.lines\.\d+$/.test(at) ? [`${at} = ${value.slice(0, 60)}`] : []
  if (typeof value === 'object') return Object.entries(value).flatMap(([key, inner]) => unread(inner, `${at}.${key}`, depth + 1))
  return []
}

/** What the views read of the app: the monitor's face and strip, the shell's state, every place's placeholder, the session's view state (its header, transcript and composer, design 1u), the Sessions place's list, inspector and door and the console (1g), the Models place's every tab, sheet and confirm and the sidebar's running counts (1h), and the Settings window's every pane and its two confirms' words (1i). */
function whatTheViewsRead(app: AppViewModel) {
  const shell = app.shell
  return {
    monitor: app.monitor.face(),
    strip: app.strip ?? { summary: 'not read yet' },
    shell: { route: shell.route, overlay: shell.overlay ?? 'none', systemOpen: shell.systemOpen, layout: shell.layout, themeName: shell.themeName, toast: shell.toast?.text ?? 'none', sheet: shell.sheet ?? 'none', confirm: shell.confirm ?? 'none' },
    places: destinations.map((destination) => placeholderOf(destination)?.says ?? 'ported'),
    session: app.session.face(),
    sessions: { list: app.library.listFace(), inspector: app.library.inspector(), door: app.library.door(), info: app.library.info ?? 'none', renaming: app.library.renaming ?? 'none' },
    console: app.console.face(),
    // The Models place (1h): its tabs, Backends, Stored weights, Jobs, Roles and its sheets, and its confirms' words and the open store sheet.
    models: app.models.face(),
    modelsSheet: app.models.store.sheet() ?? 'none',
    modelsConfirms: (['model', 'storeDelete', 'storeModelDelete', 'unregister', 'registerAll'] as const).map((kind) => app.models.confirmWords(kind) ?? 'none'),
    running: app.placeRuns,
    variables: app.theme.variables,
    settings: app.settings.face(),
    settingsConfirms: (['settingsForget', 'xcodeRemove'] as const).map((kind) => app.settings.confirmWords(kind) ?? 'none'),
    // The Training place (1j): every tab, its sheets and its two confirms' words.
    training: app.training.face(),
    // The Video place (1k): the clips, the run, the clip page, the form, the install card and the consent.
    video: app.video.face(),
    // The compositions (1l): the list, the new composition, a project's page and Make changes.
    compose: app.compose.face(),
    // The Voice place (1m): the speeches, the speak card and the selected speech.
    voice: app.voice.face(),
    trainingConfirms: (['train', 'promoteRefused'] as const).map((kind) => app.training.confirmWords(kind) ?? 'none'),
    // The Library place (1n): its tab, list and page, the editor sheet and its two confirms' words.
    definitions: app.definitions.face(),
    editor: app.definitions.editorFace(),
    definitionsConfirms: (['editorClose', 'mcpRemove'] as const).map((kind) => app.definitions.confirmWords(kind) ?? 'none'),
    // Review (1n): its three tabs, the add-target sheet and the post sheet's words.
    review: app.review.face(),
    targetSheet: app.review.targetSheet(),
    post: app.review.postWords ?? 'none',
    // Knowledge and the Code graph (1o): both tabs, the graph's every view part, its save panel, dedupe sheet and two confirms' words.
    records: app.records.face(),
    collections: app.collections.face(),
    graph: app.graph.face(),
    savePanel: app.graph.savePanel(),
    dedupe: app.graph.dedupeSheet(),
    graphConfirms: (['graphDelete', 'explainRefused'] as const).map((kind) => app.graph.confirmWords(kind) ?? 'none'),
    // Suites and Tasks (1p): the list and a suite's page; the ledger, a task's page, the New task sheet and the refusal's words.
    suites: app.suites.face(),
    tasks: app.tasks.face(),
    newTask: app.tasks.sheetFace(),
    tasksConfirm: app.tasks.confirmWords('taskRefused') ?? 'none',
  }
}

const modes: readonly ApogeeMode[] = ['installed', 'missing', 'config-typo']

/**
 * Starts `app` at its preset, walks what the views read still and after 30
 * ticks, and returns what nobody answered. With `pressSuite`, the suite
 * control is pressed after the still walk wherever the face offers it (the
 * empty state's Choose a suite, the header's suite, `/suite`), so its reads
 * are answered too.
 */
async function walk(app: AppViewModel, name: string, pressSuite = false): Promise<readonly string[]> {
  await app.start()
  await new Promise((resolve) => setTimeout(resolve, 0))
  // The inspector's reads, made whether or not the preset opens it (⌥⌘I opens it on every one).
  await app.library.inspectorAppeared()
  // The Models place's reads too (1h): it opens from every screen.
  await app.models.appeared()
  // The Training place's too (1j): ⌘9 opens it on every screen.
  await app.training.appeared()
  // The Video place's too (1k): ⌘6 opens it on every screen; and its Compositions tab's (1l).
  await app.video.appeared()
  await app.compose.appeared()
  // The Voice place's too (1m): ⌘7 opens it on every screen.
  await app.voice.appeared()
  // Library's and Review's too (1n): ⌘0 and ⇧⌘R open them on every screen.
  await app.definitions.appeared()
  await app.review.appeared()
  // Knowledge's and the Code graph's too (1o): ⌘8 and ⌘5 open them on every screen, the graph in each of its views.
  await app.records.appeared()
  await app.collections.appeared()
  const view = app.graph.view
  for (const each of ['functions', 'files', 'prose'] as const) await app.graph.show(each)
  await app.graph.show(view)
  // Suites' and Tasks' too (1p): ⌘3 and ⌘4 open them on every screen; the New task sheet's pickers read, each recipe picked, then the sheet as it was.
  await app.suites.appeared()
  await app.tasks.appeared()
  const sheetOpen = app.tasks.form !== undefined
  if (!sheetOpen) await app.tasks.openNew()
  for (const recipe of ['capture-decisions', 'changelog-draft', 'digest-folder']) await app.tasks.pickRecipe(recipe)
  if (!sheetOpen) app.tasks.cancelNew()
  // The Settings window's too (1i): ⌘, opens it on every screen; each pane shown, so each one's own reads are made.
  app.openSettings(app.settings.pane)
  for (const [pane] of settingsPanes) app.settings.pick(pane)
  await app.settings.appeared()
  await new Promise((resolve) => setTimeout(resolve, 0))
  expect(unread(whatTheViewsRead(app), name)).toEqual([])
  const face = app.session.face()
  if (pressSuite && (face.empty.chooseSuite || face.header.canSwitchSuite)) {
    app.session.chooseSuite()
    await new Promise((resolve) => setTimeout(resolve, 0))
  }
  app.clock.advance(30)
  await new Promise((resolve) => setTimeout(resolve, 0))
  expect(unread(whatTheViewsRead(app), name)).toEqual([])
  app.stop()
  // The console's own child too (1g): its lines are answered apart from the places'.
  return [...app.child.unanswered, ...app.consoleChild.unanswered]
}

describe("the presets: the old prototype's 30-tick check's successor", () => {
  test('the table holds the monitor, the session’s thirteen in the old data-props order -- 1u’s four plain ones and 1v’s nine scenarios and states -- 1g’s execute and console, 1h’s nine Models presets, 1i’s nine Settings presets in the old order and its four new ones, 1j’s nine Training presets in the old order and its new training:regimes, 1k’s four Video presets in the old order, 1l’s five Compose presets in the old order, 1m’s voice, 1n’s six Library and four Review presets in the old order, 1o’s three Knowledge and three Code graph presets in the old order, each place’s followed by GUI M22’s runs that left a note, and 1p’s Suites preset and eight Tasks and New task presets in the old order', () => {
    expect(Object.keys(presets)).toEqual([
      'monitor',
      'chat',
      'chat:retry',
      'chat:wal',
      'chat:crash',
      'chat:pr',
      'chat:new',
      'chat:private',
      'chat:play',
      'chat:suite',
      'chat:restarting',
      'chat:notsent',
      'chat:ended',
      'chat:compacting',
      'execute',
      'console',
      'models',
      'models:downloads',
      'models:store',
      'models:convert',
      'models:register',
      'models:roles',
      'models:delete',
      'models:notran',
      'download',
      'settings:general',
      'settings:appearance',
      'settings:media',
      'settings:backends',
      'settings:fixing',
      'settings:fixfailed',
      'settings:permissions',
      'settings:nosearch',
      'settings:xcode',
      'settings:roles',
      'settings:web',
      'settings:memory',
      'settings:keyboard',
      'training',
      'training:datasets',
      'training:pipelines',
      'training:cycle',
      'training:stalelock',
      'training:unreadable',
      'training:cyclepass',
      'training:passrunning',
      'training:resumeheld',
      'training:regimes',
      'video',
      'video:run',
      'video:install',
      'consent',
      'compose',
      'compose:edit',
      'compose:new',
      'compose:tide',
      'compose:differs',
      'voice',
      'library',
      'library:symphonies',
      'library:mcp',
      'library:unsaved',
      'library:refused',
      'library:image',
      'review',
      'review:post',
      'review:targets',
      'review:skills',
      'knowledge',
      'knowledge:collections',
      'knowledge:mixed',
      'knowledge:ingested-with-a-note',
      'knowledge:captured-with-a-note',
      'graph',
      'graph:prose',
      'graph:refused',
      'graph:built-with-a-note',
      'suites',
      'tasks',
      'tasks:denied',
      'tasks:more',
      'tasks:resuming',
      'tasks:refused',
      'tasks:resumerefused',
      'newtask',
      'newtask:recipe',
    ])
  })

  test('monitor is walked with its chat rule: the Streaming session behind it, launched, and nothing left unanswered', async () => {
    const app = new AppViewModel(presetFor({ start: 'monitor', still: true, apogee: 'installed' }).preset, modeRules)
    expect(await walk(app, 'monitor', true)).toEqual([])
    expect(app.child.calls.filter((call) => call.kind === 'chats').map((call) => call.arguments[0])).toEqual(['chat'])
    expect(app.session.face().empty.launched).toBe(true)
  })

  for (const name of Object.keys(presets)) {
    for (const apogee of modes) {
      test(`${name} at ${apogee}: no throw, nothing unanswered (the suite control pressed where offered), and no undefined or NaN in what the views read, still and after 30 ticks`, async () => {
        for (const still of [true, false]) {
          const app = new AppViewModel(presetFor({ start: name, still, apogee }).preset, modeRules)
          expect(await walk(app, name, true), name).toEqual([])
        }
      })
    }
  }

  test('a planted preset whose chat rule is missing fails, naming the unanswered launch', async () => {
    const { preset } = presetFor({ start: 'chat:new', still: true, apogee: 'installed' })
    const planted = { ...preset, rules: { ...preset.rules, chats: [] } }
    const app = new AppViewModel(planted, modeRules)
    expect(await walk(app, 'planted')).toEqual(['chats: apogee chat --output-format stream-json --input-format stream-json --tools'])
  })

  test('a planted Models preset missing the selected backend’s models info fails, naming the unanswered read (1h)', async () => {
    const { preset } = presetFor({ start: 'models', still: true, apogee: 'installed' })
    const reads = (preset.rules.reads ?? []).filter((rule) => rule.arguments.join(' ') !== 'models info llama3.2-1b-Q4KM')
    const app = new AppViewModel({ ...preset, rules: { ...preset.rules, reads } }, modeRules)
    expect(new Set(await walk(app, 'planted'))).toEqual(new Set(['reads: apogee models info llama3.2-1b-Q4KM']))
  })

  test('a planted models info with a missing field draws its absence, never undefined: no model_path, so no Path, Weights or Files (1h)', async () => {
    const { preset } = presetFor({ start: 'models', still: true, apogee: 'installed' })
    const planted = (preset.rules.reads ?? []).map((rule) => {
      if (rule.arguments.join(' ') !== 'models info llama3.2-1b-Q4KM') return rule
      const answer = Array.isArray(rule.answer) ? rule.answer[0] : rule.answer
      const document = JSON.parse((answer as { stdout: string }).stdout) as { fields: Array<{ field: string }> }
      return { ...rule, answer: { stdout: JSON.stringify({ ...document, fields: document.fields.filter((field) => field.field !== 'model_path') }) } }
    })
    const app = new AppViewModel({ ...preset, rules: { ...preset.rules, reads: planted } }, modeRules)
    expect(await walk(app, 'planted')).toEqual([])
    const detail = app.models.face().backends.detail
    expect(detail.facts.map((fact) => fact.label)).not.toContain('Path')
    expect(detail.files).toEqual([])
  })

  test('a planted undefined in a clip’s facts fails the walk, naming its path (design 1k)', async () => {
    const app = new AppViewModel(presetFor({ start: 'video', still: true, apogee: 'installed' }).preset, modeRules)
    await app.start()
    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(unread(whatTheViewsRead(app), 'video')).toEqual([])
    const face = app.video.face()
    const planted = { ...face, clip: { ...face.clip, facts: [{ label: 'Seed', value: undefined, mono: true }, ...face.clip.facts.slice(1)] } }
    app.video.face = () => planted as unknown as ReturnType<typeof app.video.face>
    expect(unread(whatTheViewsRead(app), 'video')).toEqual(['video.video.clip.facts.0.value'])
  })

  test('a record missing its seed and its device draws their absence in words, never undefined (design 1k)', async () => {
    const { preset } = presetFor({ start: 'video', still: true, apogee: 'installed' })
    const folders = Object.fromEntries(
      Object.entries(preset.folders ?? {}).map(([folder, files]) => [
        folder,
        files.map((file) => {
          if (file.text === undefined || !file.name.startsWith('lighthouse-dusk.mp4')) return file
          const record = JSON.parse(file.text) as Record<string, unknown>
          delete record.seed
          delete record.device
          return { ...file, text: JSON.stringify(record) }
        }),
      ]),
    )
    const app = new AppViewModel({ ...preset, folders }, modeRules)
    await app.start()
    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(unread(whatTheViewsRead(app), 'video')).toEqual([])
    expect(app.video.face().clip.facts.find((fact) => fact.label === 'Seed')?.value).toBe('not recorded · held')
  })

  test('a planted record missing its fields, and a graph node missing its file and line, draw their absence in words, never undefined (design 1o)', async () => {
    const { preset } = presetFor({ start: 'knowledge', still: true, apogee: 'installed' })
    const planted = (rules: readonly { arguments: readonly string[]; answer: unknown }[] | undefined, command: string, change: (document: Record<string, unknown>) => unknown) =>
      (rules ?? []).map((rule) => {
        if (rule.arguments.join(' ') !== command) return rule
        const answer = rule.answer as { stdout: string }
        return { ...rule, answer: { stdout: JSON.stringify(change(JSON.parse(answer.stdout) as Record<string, unknown>)) } }
      })
    const reads = planted(preset.rules.reads as never, 'knowledge list', (document) => ({ ...document, data: [{ id: 'kr-planted' }] }))
    const pictured = planted(reads as never, 'graph query --graph demo -- runner fetch', (document) => ({ ...document, entities: [{ name: 'ingest.planted', type: 'function' }] }))
    const app = new AppViewModel({ ...preset, rules: { ...preset.rules, reads: pictured as never } }, modeRules)
    expect(await walk(app, 'planted')).toEqual([])
    app.records.select('kr-planted')
    await app.graph.show('functions')
    await app.graph.select('ingest.planted')
    expect(unread(whatTheViewsRead(app), 'planted')).toEqual([])
    expect(app.records.face().record.facts.find((fact) => fact.label === 'LINKED TO')?.value).toBe('nothing yet')
    expect([app.graph.face().code.card.label, app.graph.face().code.card.where]).toEqual(['ingest.planted', ''])
  })

  test('a planted undefined in a node’s card fails the walk, naming its path (design 1o)', async () => {
    const app = new AppViewModel(presetFor({ start: 'graph', still: true, apogee: 'installed' }).preset, modeRules)
    await app.start()
    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(unread(whatTheViewsRead(app), 'graph')).toEqual([])
    const face = app.graph.face()
    app.graph.face = () => ({ ...face, code: { ...face.code, card: { ...face.code.card, sig: undefined } } }) as unknown as ReturnType<typeof app.graph.face>
    expect(unread(whatTheViewsRead(app), 'graph')).toEqual(['graph.graph.code.card.sig'])
  })

  test('the walk finds what it looks for', () => {
    expect(unread({ a: 1, b: { c: Number.NaN, d: 'x undefined', e: undefined } }, 'p')).toEqual(['p.b.c', 'p.b.d = x undefined', 'p.b.e'])
  })
})
