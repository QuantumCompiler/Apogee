import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { windowKeys } from '../../../source/presentation/app/ViewModels/AppViewModel'
import { CodeText } from '../../../source/presentation/gallery/Components/CodeText'
import { contractRows, contractGaps, contractDecisions, contractCalls, startingUp } from '../../../source/presentation/gallery/Models/contract'
import { previewKeys, previewParts } from '../../../source/presentation/gallery/Previews/GalleryPreview'
import { BuildContractViewModel } from '../../../source/presentation/gallery/ViewModels/BuildContractViewModel'
import { BuildLayoutViewModel } from '../../../source/presentation/gallery/ViewModels/BuildLayoutViewModel'
import { BuildProcessesViewModel } from '../../../source/presentation/gallery/ViewModels/BuildProcessesViewModel'
import { codeRuns } from '../../../source/presentation/gallery/ViewModels/CodeRuns'
import { GalleryViewModel } from '../../../source/presentation/gallery/ViewModels/GalleryViewModel'
import { BuildContractPage } from '../../../source/presentation/gallery/Views/BuildContractPage'
import { BuildLayoutPage } from '../../../source/presentation/gallery/Views/BuildLayoutPage'
import { BuildProcessesPage } from '../../../source/presentation/gallery/Views/BuildProcessesPage'
import { GalleryView } from '../../../source/presentation/gallery/Views/GalleryView'
import { keyboardLines, KeyMap } from '../../../source/presentation/shell/ViewModels/KeyMap'

/** What a page reads as: its markup's text, tags out, entities read. */
function text(markup: string): string {
  return markup
    .replace(/<[^>]+>/g, '')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#x27;/g, "'")
    .replace(/&amp;/g, '&')
}

/** Data's words as drawn: the backticks are the code span's, not the text's. */
const drawn = (words: string) => words.replaceAll('`', '')

const gallery = (search: string) => new GalleryViewModel({ search, replace: () => undefined })

describe('the inline code span (design 1r): backticks in the data, one shared span on the page', () => {
  test('a text split into its plain and code runs', () => {
    expect(codeRuns('send `{"type":"cancel"}`; the turn ends in one `result`')).toEqual([
      { text: 'send ', code: false },
      { text: '{"type":"cancel"}', code: true },
      { text: '; the turn ends in one ', code: false },
      { text: 'result', code: true },
    ])
    expect(codeRuns('no code here')).toEqual([{ text: 'no code here', code: false }])
  })

  test('drawn as the code face for each code run, and plain words between', () => {
    const markup = renderToStaticMarkup(<CodeText text="the `session` event" />)
    expect(markup).toMatch(/^the <span class="mono"[^>]*>session<\/span> event$/)
  })
})

describe('the contract page, drawn (design 1r)', () => {
  const markup = renderToStaticMarkup(<BuildContractPage model={new BuildContractViewModel()} />)
  const words = text(markup)

  test('every step, row, gap, decision and call, in its words; the code in the code face', () => {
    for (const step of startingUp) expect(words).toContain(`${step.lead} ${drawn(step.text)}`)
    for (const row of contractRows) {
      expect(words).toContain(drawn(row.shows))
      expect(words).toContain(drawn(row.how))
    }
    for (const gap of contractGaps) {
      expect(words).toContain(drawn(gap.title))
      expect(words).toContain(drawn(gap.change))
      expect(words).toContain(drawn(gap.when === 'Closed' ? `Closed: ${gap.note}` : `Until then: ${gap.fallback}`))
    }
    for (const lead of [...contractDecisions, ...contractCalls]) expect(words).toContain(`${lead.lead} ${drawn(lead.text)}`)
    expect(markup).toMatch(/<span class="mono"[^>]*>chats list --output-format json<\/span>/)
  })

  test('the stat cards show the counts: 87, 13, 16 and 0', () => {
    expect(markup.match(/data-card="[a-z]+"[^>]*>[^<]*<span[^>]*>\d+<\/span>/g)?.map((card) => /data-card="([a-z]+)"/.exec(card)?.[1] + ' ' + />(\d+)<\/span>$/.exec(card)?.[1])).toEqual(['ready 87', 'app 13', 'gap 16', 'verify 0'])
    expect(words).toContain('16gaps open, each an additive CLI change')
  })

  test('a gap chip is a link to its gap’s row, which carries its anchor', () => {
    expect(markup).toContain('href="#contract-gap-G19"')
    expect(markup).toContain('id="contract-gap-G19"')
    expect(markup.match(/href="#contract-gap-G\d+"/g)).toHaveLength(16)
  })

  test('the parts under their labels, the three headers, and a closed gap dimmed', () => {
    for (const part of ['SESSIONS', 'MODELS', 'SETTINGS AND SYSTEM']) expect(markup).toContain(`>${part}</h3>`)
    expect(words).toContain('THE DESIGN SHOWSHOW THE APP GETS ITSTATUS')
    expect(markup).toMatch(/id="contract-gap-G1"[^>]*opacity:0.6/)
    expect(markup).not.toMatch(/id="contract-gap-G18"[^>]*opacity/)
  })
})

describe('the layout page, drawn (design 1r)', () => {
  const model = new BuildLayoutViewModel(keyboardLines(KeyMap.reference, windowKeys))
  const markup = renderToStaticMarkup(<BuildLayoutPage model={model} />)

  test('the native controls under SWIFTUI · MACOS 27, never 14', () => {
    expect(markup).toContain('SWIFTUI · MACOS 27')
    expect(markup).not.toContain('MACOS 14')
    expect(text(markup)).toContain('Win2D CanvasControl')
  })

  test('the window at three quarters, each box at its place and size, its label and note', () => {
    expect(markup).toMatch(/left:0;top:0;width:192px;height:675px[^"]*"[^>]*><span><b>Sidebar · 256<\/b>/)
    expect(markup).toMatch(/left:810px;top:39px;width:270px;height:636px/)
    expect(text(markup)).toContain('Places · 40 × 34 each')
  })

  test('every region, key row, state, type size and space row in its words; each key from the key map', () => {
    const face = model.face()
    const words = text(markup)
    for (const region of face.regions) expect(words).toContain(`${region.name}${region.size}${region.words}`)
    for (const row of face.keys) expect(words).toContain(`${row.words}${row.keys}`)
    for (const state of face.states) expect(words).toContain(`${state.name}${state.words}`)
    for (const size of face.type) {
      expect(words).toContain(`${size.variable}${size.sample}${size.px}`)
      expect(markup).toContain(`font-size:var(${size.variable})`)
    }
    for (const row of face.space) expect(words).toContain(`${row.name}${row.words}`)
  })
})

describe('the processes page, drawn (design 1r)', () => {
  const markup = renderToStaticMarkup(<BuildProcessesPage model={new BuildProcessesViewModel()} />)

  test('its title, its words and every box', () => {
    const words = text(markup)
    expect(words).toContain('How the app runs Apogee')
    expect(words).toContain('only apogee serve owns one')
    for (const box of new BuildProcessesViewModel().face().boxes) expect(words).toContain(box.title)
    expect(words).toContain('a protocol client generated from machine-schema.json')
  })

  test('its ten connectors and six arrowheads, in the connector token; no hex anywhere', () => {
    expect(markup.match(/<line /g)).toHaveLength(10)
    expect(markup.match(/<path /g)).toHaveLength(6)
    expect(markup).toContain('stroke-dasharray="6 6"')
    expect(markup).toContain('color:var(--text-tertiary)')
    expect(markup).toContain('border:1px solid var(--status-accent)')
    expect(markup).not.toMatch(/#[0-9a-fA-F]{3,8}\b/)
  })
})

describe('the Build row in the gallery (design 1r)', () => {
  test("Contract, Layout and Processes draw their pages where 1q's card stood", () => {
    for (const [id, words] of [
      ['BuildContract', 'How the app talks to Apogee'],
      ['BuildLayout', 'SWIFTUI · MACOS 27'],
      ['BuildProcesses', 'stores re-read from the CLI'],
    ] as const) {
      const markup = renderToStaticMarkup(<GalleryView model={gallery(`?gallery&board=${id}`)} parts={previewParts} />)
      expect(markup, id).toContain(words)
      expect(markup, id).not.toContain('Not ported yet')
    }
  })

  test('no page pins a theme of its own: each draws in whatever theme it sits in', () => {
    for (const page of [<BuildContractPage model={new BuildContractViewModel()} />, <BuildLayoutPage model={new BuildLayoutViewModel(previewKeys)} />, <BuildProcessesPage model={new BuildProcessesViewModel()} />]) {
      expect(renderToStaticMarkup(page)).not.toContain('data-theme')
    }
  })

  test('a page takes its board’s theme', () => {
    const model = gallery('?gallery&board=BuildProcesses')
    model.setTheme('BuildProcesses', 'light')
    expect(renderToStaticMarkup(<GalleryView model={model} parts={previewParts} />)).toMatch(/data-theme="light"[^>]*>.*How the app runs Apogee/)
  })

  test('a Build page at full size takes the pointer, so its gap links work; a screen stays inert', () => {
    const at = (id: string) => renderToStaticMarkup(<GalleryView model={gallery(`?gallery&board=${id}`)} parts={previewParts} />)
    for (const id of ['BuildContract', 'BuildLayout', 'BuildProcesses']) expect(at(id), id).not.toContain('inert=""')
    expect(at('ScreenModels')).toContain('inert=""')
    expect(at('ColorTokens')).toContain('inert=""')
  })

  test("the previews' key map is the one app hands in", () => {
    expect(previewKeys).toEqual(keyboardLines(KeyMap.reference, windowKeys))
    expect(previewParts.keys).toBe(previewKeys)
  })
})
