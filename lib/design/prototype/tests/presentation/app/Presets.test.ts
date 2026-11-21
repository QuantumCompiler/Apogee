import { describe, expect, test } from 'vitest'
import { presetFor, presets } from '../../../source/presentation/app/Previews/Presets'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { AppViewModel } from '../../../source/presentation/app/ViewModels/AppViewModel'
import { destinations, placeholderOf } from '../../../source/presentation/shell/ViewModels/Destination'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'

/** Every path in `value` that reads `undefined`, `null` or `NaN`, or a string that says so: the old 30-tick check's walk. */
function unread(value: unknown, at: string, depth = 0): string[] {
  if (depth > 9) return []
  if (value === undefined || value === null) return [at]
  if (typeof value === 'number') return Number.isNaN(value) ? [at] : []
  if (typeof value === 'string') return /undefined|NaN|null/.test(value) ? [`${at} = ${value.slice(0, 60)}`] : []
  if (typeof value === 'object') return Object.entries(value).flatMap(([key, inner]) => unread(inner, `${at}.${key}`, depth + 1))
  return []
}

/** What the views read of the app: the monitor's face and strip, the shell's state, every place's placeholder, the session's view state (its header, transcript and composer, design 1u), the Sessions place's list, inspector and door and the console (1g), and the Models place's every tab, sheet and confirm and the sidebar's running counts (1h). */
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
  test('the table holds the monitor, the session’s thirteen in the old data-props order -- 1u’s four plain ones and 1v’s nine scenarios and states -- 1g’s execute and console, and 1h’s nine Models presets', () => {
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

  test('the walk finds what it looks for', () => {
    expect(unread({ a: 1, b: { c: Number.NaN, d: 'x undefined', e: undefined } }, 'p')).toEqual(['p.b.c', 'p.b.d = x undefined', 'p.b.e'])
  })
})
