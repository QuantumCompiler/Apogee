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

/** What the views read of the app: the monitor's face and strip, the shell's state, every place's placeholder. */
function whatTheViewsRead(app: AppViewModel) {
  const shell = app.shell
  return {
    monitor: app.monitor.face(),
    strip: app.strip ?? { summary: 'not read yet' },
    shell: { route: shell.route, overlay: shell.overlay ?? 'none', systemOpen: shell.systemOpen, layout: shell.layout, themeName: shell.themeName, toast: shell.toast?.text ?? 'none' },
    places: destinations.map((destination) => placeholderOf(destination)?.says ?? 'ported'),
    variables: app.theme.variables,
  }
}

const modes: readonly ApogeeMode[] = ['installed', 'missing', 'config-typo']

describe("the presets: the old prototype's 30-tick check's successor", () => {
  for (const name of Object.keys(presets)) {
    for (const apogee of modes) {
      test(`${name} at ${apogee}: no throw, nothing unanswered, and no undefined or NaN in what the view reads, still and after 30 ticks`, async () => {
        for (const still of [true, false]) {
          const app = new AppViewModel(presetFor({ start: name, still, apogee }).preset, modeRules)
          app.start()
          await new Promise((resolve) => setTimeout(resolve, 0))
          expect(unread(whatTheViewsRead(app), name)).toEqual([])
          app.clock.advance(30)
          await new Promise((resolve) => setTimeout(resolve, 0))
          expect(unread(whatTheViewsRead(app), name)).toEqual([])
          expect(app.child.unanswered).toEqual([])
          app.stop()
        }
      })
    }
  }

  test('the walk finds what it looks for', () => {
    expect(unread({ a: 1, b: { c: Number.NaN, d: 'x undefined', e: undefined } }, 'p')).toEqual(['p.b.c', 'p.b.d = x undefined', 'p.b.e'])
  })
})
