import { describe, expect, test } from 'vitest'
import { ThemeSettings } from '../../../source/presentation/settings/ViewModels/ThemeSettings'
import { ThemeService } from '../../../source/presentation/styles/Appearance/ThemeService'
import { defaultAppearance } from '../../../source/presentation/styles/Appearance/Appearance'
import { frozenComponent } from '../styles/Frozen'

/** The old pane's values, as the frozen prototype's renderVals() builds them at `start`, after `state` is set. */
function oldPane(start = 'settings:appearance', state: Record<string, unknown> = {}) {
  const old = frozenComponent({ start, still: true }) as unknown as { state: Record<string, unknown>; renderVals(): { set: Record<string, unknown> } }
  Object.assign(old.state, state)
  return old.renderVals().set as {
    themeCards: Array<{ name: string; note: string; current: boolean; s1: string; s2: string; s3: string; s4: string; s5: string; s6: string }>
    sizes: string
    editName: string
    editFile: string
    changesText: string
    lowText: string
    groups: Array<{ title: string; rows: Array<{ key: string; label: string; where: string; ratioText: string; badgeNote: string; badgeTitle: string; hexShown: string; baseHex: string; edited: boolean }> }>
  }
}

const fresh = () => {
  const service = new ThemeService()
  return { service, model: new ThemeSettings(service) }
}

describe('Settings › Appearance, driven through its view-model (design 1i)', () => {
  test("its face is the old pane's at the start: the four theme cards, the sizes line, the edited theme's colors with their contrast", () => {
    const { model } = fresh()
    const face = model.face()
    const old = oldPane()
    expect(face.cards.map((card) => [card.name, card.note, card.current])).toEqual(old.themeCards.map((card) => [card.name, card.note, card.current]))
    expect(face.cards.map((card) => card.swatches.map((swatch) => swatch.toLowerCase()))).toEqual(old.themeCards.map((card) => [card.s1, card.s2, card.s3, card.s4, card.s5, card.s6]))
    expect(face.sizesLine).toBe(`Answers ${old.sizes}. Fonts and size are saved in config, not in themes, so a shared theme never changes your text.`)
    expect([face.colors.title, face.colors.file, face.colors.changes, face.colors.low]).toEqual([`ELEMENT COLORS · ${old.editName}`, old.editFile, old.changesText, old.lowText])
    expect(face.colors.groups.map((group) => [group.title, group.rows.map((row) => [row.key, row.label, row.where, row.ratioText, row.badgeNote, row.badgeTitle, row.hexShown, row.baseHex])])).toEqual(
      old.groups.map((group) => [group.title, group.rows.map((row) => [row.key, row.label, row.where, row.ratioText, row.badgeNote, row.badgeTitle, row.hexShown, row.baseHex])]),
    )
  })

  test('an override changes the matching variable for the whole window, through the theme service', () => {
    const { service, model } = fresh()
    model.pickColor('status.accent', '#FF8800')
    expect(service.appearance.overrides.dark['status.accent']).toBe('#ff8800')
    expect(service.variables).toContain('--status-accent: #ff8800')
    expect(service.style['--status-accent']).toBe('#ff8800')
    const row = model.face().colors.groups[0]?.rows[0]
    expect(row).toMatchObject({ key: 'status.accent', value: '#ff8800', edited: true, hexShown: '#FF8800' })
    expect(model.face().colors.changes).toBe('1 change')
    expect(model.face().cards.find((card) => card.id === 'dark')?.note).toBe('1 change')
  })

  test("a value equal to the theme's own drops the override, as withOverride() does", () => {
    const { service, model } = fresh()
    model.pickColor('status.accent', '#ff8800')
    model.pickColor('status.accent', '#7fd4c1')
    expect(service.appearance.overrides.dark).toEqual({})
    expect(model.face().colors.changes).toBe('no changes')
  })

  test('the hex field: a draft is kept as typed and applied once it is six hex digits, with or without its #; leaving it drops the draft', () => {
    const { service, model } = fresh()
    model.typeHex('status.error', '#12ab')
    expect(service.appearance.overrides.dark).toEqual({})
    expect(model.face().colors.groups[0]?.rows[2]?.hexShown).toBe('#12ab')
    model.typeHex('status.error', '12ab34')
    expect(service.appearance.overrides.dark['status.error']).toBe('#12ab34')
    model.leaveHex('status.error')
    expect(model.face().colors.groups[0]?.rows[2]?.hexShown).toBe('#12AB34')
  })

  test("a row's reset puts the theme's own color back, and Reset all every change of the theme edited, its drafts with them", () => {
    const { service, model } = fresh()
    model.pickColor('status.accent', '#ff8800')
    model.pickColor('text.primary', '#ffffff')
    model.typeHex('status.error', '#12')
    model.reset('status.accent')
    expect(service.appearance.overrides.dark).toEqual({ 'text.primary': '#ffffff' })
    model.resetAll()
    expect(service.appearance.overrides.dark).toEqual({})
    expect(model.face().colors.noChanges).toBe(true)
    expect(model.face().colors.groups[0]?.rows[2]?.hexShown).toBe('#F2836F')
  })

  test('the contrast badge marks a planted low pair: too low under 3:1, low under 4.5:1, counted in the heading', () => {
    const { model } = fresh()
    // Tertiary text near the window's own grey: 1.2:1.
    model.pickColor('text.tertiary', '#202122')
    // Accent dimmed to 3.6:1 against the window.
    model.pickColor('status.accent', '#4f8478')
    const rows = model.face().colors.groups.flatMap((group) => group.rows)
    expect(rows.find((row) => row.key === 'text.tertiary')).toMatchObject({ badgeNote: 'too low', badgeColor: 'var(--status-error)' })
    expect(rows.find((row) => row.key === 'status.accent')).toMatchObject({ badgeNote: 'low', badgeColor: 'var(--status-warning)' })
    expect(model.face().colors).toMatchObject({ low: '2 elements below 4.5:1', lowColor: 'var(--status-warning)' })
    const old = oldPane('settings:appearance', { overrides: { dark: { 'text.tertiary': '#202122', 'status.accent': '#4f8478' }, light: {}, contrast: {} } })
    expect(old.lowText).toBe('2 elements below 4.5:1')
    expect(old.groups.flatMap((group) => group.rows).filter((row) => row.badgeNote !== '').map((row) => [row.key, row.badgeNote])).toEqual([
      ['status.accent', 'low'],
      ['text.tertiary', 'too low'],
    ])
  })

  test("each of the 4 text sizes scales the type by the prototype's factors, rounded to 0.5 px; at Medium prose is 14, body 13 and code 12", () => {
    const { service, model } = fresh()
    const px = (step: string) => Number(service.style[`--t-${step}`]?.replace('px', ''))
    expect([px('prose'), px('body'), px('code'), px('dense')]).toEqual([14, 13, 12, 12.5])
    const expected: Record<string, [number, number, number]> = { s: [13, 12, 11], m: [14, 13, 12], l: [15, 14, 13], xl: [16, 15, 14] }
    for (const [size, [prose, body, code]] of Object.entries(expected)) {
      model.setTextSize(size as 's' | 'm' | 'l' | 'xl')
      expect([px('prose'), px('body'), px('code')], size).toEqual([prose, body, code])
      expect(model.face().sizesLine.startsWith(`Answers ${prose} px · interface ${body} px · code ${code} px.`)).toBe(true)
      expect(model.face().sizes.find((choice) => choice.current)?.value).toBe(size)
    }
    expect(px('label')).toBe(Math.round(10.5 * (15 / 13) * 2) / 2)
  })

  test('the two fonts change the window\'s font variables', () => {
    const { service, model } = fresh()
    model.setUiFont('atkinson')
    model.setCodeFont('jetbrains')
    expect(service.style['--font-ui']).toBe("'Atkinson Hyperlegible', system-ui, sans-serif")
    expect(service.style['--font-mono']).toBe("'JetBrains Mono', ui-monospace, Menlo, monospace")
    expect(model.face().fonts).toMatchObject({ ui: 'atkinson', code: 'jetbrains' })
    expect(model.face().fonts.uiOptions.map((option) => option.label)).toEqual(['IBM Plex Sans', 'System font (SF Pro)', 'Atkinson Hyperlegible'])
    expect(model.face().fonts.codeOptions.map((option) => option.label)).toEqual(['IBM Plex Mono', 'System mono (SF Mono)', 'JetBrains Mono'])
  })

  test('a theme card picks the theme; the edited colors are the theme drawn, and each theme keeps its own', () => {
    const { service, model } = fresh()
    model.pickColor('status.accent', '#ff8800')
    model.pick('light')
    expect(service.theme).toBe('light')
    expect(model.face().colors.title).toBe('ELEMENT COLORS · GRAPHITE LIGHT')
    expect(model.face().colors.changes).toBe('no changes')
    expect(model.face().cards.map((card) => card.note)).toEqual(['Follows macOS', '1 change', 'Built-in', 'Built-in'])
  })

  test("Automatic: its line says what the viewer's setting is now, and its two picks map light and dark", () => {
    const { service, model } = fresh()
    model.pick('auto')
    expect(model.face().auto).toMatchObject({ shown: true, line: 'Automatic follows macOS. It’s dark right now.', light: 'light', dark: 'dark' })
    model.setAuto('dark', 'contrast')
    expect(service.theme).toBe('contrast')
    service.systemChanged('light')
    model.setAuto('light', 'dark')
    expect(model.face().auto.line).toBe('Automatic follows macOS. It’s light right now.')
    expect(service.theme).toBe('dark')
    expect(model.face().auto.lightOptions.map((option) => option.label)).toEqual(['Graphite Light', 'Graphite Dark', 'High Contrast'])
    expect(model.face().auto.darkOptions.map((option) => option.label)).toEqual(['Graphite Dark', 'Graphite Light', 'High Contrast'])
  })

  test('nothing is kept apart from the theme service: a second pane over the same service draws the same', () => {
    const { service, model } = fresh()
    model.pickColor('code.keyword', '#123456')
    model.setTextSize('l')
    expect(new ThemeSettings(service).face()).toEqual(model.face())
    expect(service.appearance).not.toBe(defaultAppearance)
  })
})
