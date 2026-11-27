import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { boards } from '../../../source/presentation/gallery/Models/Boards'
import { previewParts } from '../../../source/presentation/gallery/Previews/GalleryPreview'
import { colorPage } from '../../../source/presentation/gallery/ViewModels/ColorPage'
import { GalleryViewModel } from '../../../source/presentation/gallery/ViewModels/GalleryViewModel'
import { typePage } from '../../../source/presentation/gallery/ViewModels/TypePage'
import { ColorTokensPage } from '../../../source/presentation/gallery/Views/ColorTokensPage'
import { ComponentsPage } from '../../../source/presentation/gallery/Views/ComponentsPage'
import { GalleryView } from '../../../source/presentation/gallery/Views/GalleryView'
import { LogoPage } from '../../../source/presentation/gallery/Views/LogoPage'
import { TypeSpacePage } from '../../../source/presentation/gallery/Views/TypeSpacePage'

const entry = (id: string) => boards.find((each) => each.id === id)!
const gallery = (search: string) => new GalleryViewModel({ search, replace: () => undefined })

describe('the gallery’s views, drawn to markup (design 1q)', () => {
  test('the rows: the page’s headings, each board’s title as the button that opens it, a board-less preset marked, nothing mounted before it is scrolled near', () => {
    const markup = renderToStaticMarkup(<GalleryView model={gallery('?gallery')} parts={previewParts} />)
    expect(markup).toContain('Screens — sessions, the system monitor and the console')
    expect(markup).toContain('Session · saved chats compacting')
    expect(markup).toContain('NO OLD BOARD')
    expect(markup).toContain('aria-selected="true"')
    expect(markup).not.toContain('chat:crash · still')
  })

  test('a frame scrolled near mounts the app at its preset, still; the focused App board live', () => {
    const near = gallery('?gallery')
    near.near('ScreenSessionError', true)
    expect(renderToStaticMarkup(<GalleryView model={near} parts={previewParts} />)).toContain('chat:crash · still · installed')
    expect(renderToStaticMarkup(<GalleryView model={gallery('?gallery&board=App')} parts={previewParts} />)).toContain('the default start · live · installed')
  })

  test('the Build row’s Contract, Layout and Processes: their pages since 1r, no longer the "Not ported yet" card', () => {
    const markup = renderToStaticMarkup(<GalleryView model={gallery('?gallery&board=BuildLayout')} parts={previewParts} />)
    expect(markup).toContain('SWIFTUI · MACOS 27')
    expect(markup).not.toContain('Not ported yet')
  })

  test('the color page draws 75 cells: 69 ratios and 6 dashes', () => {
    const markup = renderToStaticMarkup(<ColorTokensPage face={colorPage()} />)
    // Each cell's ratio closes its line, its badge after it: the footnote's and the rules' 4.5:1 are not cells.
    expect(markup.match(/>\d+\.\d:1(?: · (?:low|too low))?<\/span>/g)).toHaveLength(69)
    expect(markup.match(/>—<\/span>/g)).toHaveLength(6)
    expect(markup).toContain('Context at 80% and above, waiting for you')
  })

  test('the type page draws every size, spacing step, radius, control height, layout size and icon', () => {
    const face = typePage()
    const markup = renderToStaticMarkup(<TypeSpacePage face={face} />)
    for (const size of face.sizes) expect(markup).toContain(`>${size.step}<`)
    expect(markup).toContain('13 steps, from 2 to 32 px.')
    for (const radius of face.radii) expect(markup).toContain(`var(--radius-${radius.name})`)
    expect(markup).toContain('settings.width')
    expect(markup.match(/<svg/g)?.length).toBe(face.icons.length)
  })

  test('each Components page’s heading is its old board’s, word for word, not its row label', () => {
    expect(renderToStaticMarkup(<ComponentsPage entry={entry('ComponentsModel')} specimens={[]} />)).toMatch(/<h1[^>]*>Model, context and sources<\/h1>/)
    expect(renderToStaticMarkup(<ComponentsPage entry={entry('ComponentsTurn')} specimens={[]} />)).toMatch(/<h1[^>]*>The turn<\/h1>/)
    expect(renderToStaticMarkup(<ComponentsPage entry={entry('ComponentsPrompts')} specimens={[]} />)).toMatch(/<h1[^>]*>Asking and failing<\/h1>/)
    expect(renderToStaticMarkup(<ComponentsPage entry={entry('ComponentsMedia')} specimens={[]} />)).toMatch(/<h1[^>]*>Making media<\/h1>/)
  })

  test('a Components section no module draws: the "Not ported yet" card, naming the item that ports it', () => {
    expect(renderToStaticMarkup(<ComponentsPage entry={entry('ComponentsPrompts')} specimens={[]} />)).toContain('Not ported yet: item 1v')
  })

  test('the logo page draws the artwork it is handed, full bleed and in its rounded icon form', () => {
    const markup = renderToStaticMarkup(<LogoPage logo="data:image/png;base64,AAAA" />)
    expect(markup.match(/src="data:image\/png;base64,AAAA"/g)).toHaveLength(3)
    expect(markup).toContain('border-radius:22.4%')
  })
})

describe('the pages’ landmarks, as the old boards’ (design 1s)', () => {
  test('each page’s header is its banner, as the old board’s top-level header is, even inside the gallery’s focused board; no section is a named region', async () => {
    const { BuildContractPage } = await import('../../../source/presentation/gallery/Views/BuildContractPage')
    const { BuildLayoutPage } = await import('../../../source/presentation/gallery/Views/BuildLayoutPage')
    const { BuildContractViewModel } = await import('../../../source/presentation/gallery/ViewModels/BuildContractViewModel')
    const { BuildLayoutViewModel } = await import('../../../source/presentation/gallery/ViewModels/BuildLayoutViewModel')
    const { previewKeys } = await import('../../../source/presentation/gallery/Previews/GalleryPreview')
    const pages = [
      renderToStaticMarkup(<ColorTokensPage face={colorPage()} />),
      renderToStaticMarkup(<TypeSpacePage face={typePage()} />),
      renderToStaticMarkup(<ComponentsPage entry={entry('ComponentsModel')} specimens={previewParts.specimens} />),
      renderToStaticMarkup(<LogoPage logo="data:image/png;base64,AAAA" />),
      renderToStaticMarkup(<BuildContractPage model={new BuildContractViewModel()} />),
      renderToStaticMarkup(<BuildLayoutPage model={new BuildLayoutViewModel(previewKeys)} />),
    ]
    for (const markup of pages) {
      expect(markup).toMatch(/<header role="banner"/)
      expect(markup).not.toMatch(/<section[^>]*aria-label=/)
    }
  })
})

describe('the type page keeps the old board’s own words where they still hold (design 1s)', () => {
  test('its sentences, labels and headings are the old TypeSpace board’s, beside what 1q computes from the tokens, the two the new page outgrew said of it: its faces and its four text sizes', () => {
    const markup = renderToStaticMarkup(<TypeSpacePage face={typePage()} />)
    for (const words of [
      'Every size here moves with Settings › Appearance › Text size; spacing and shape stay fixed.',
      'IBM Plex Sans and Mono, the defaults, ship inside the app on macOS, Windows and Linux, so a layout measures the same everywhere. Settings › Appearance can swap either for the other faces here.',
      'Sizes at Medium, the default. The last column shows where each size lands at Small, Medium, Large and Extra large. Nothing goes below 9 px.',
      'Small radii, 1 px borders, and shadows only on things that float above the window.',
      'Color step plus a 1 px border. Everything in the window.',
      'One set, drawn for Apogee and shipped on every platform: 16 px grid, 1.5 px stroke, round caps. They take the color of their text.',
    ]) {
      expect(markup).toContain(words)
    }
    expect(markup).toMatch(/<h2[^>]*>Window layout<\/h2>/)
    for (const label of ['RADIUS', 'CONTROL HEIGHT', 'ELEVATION']) expect(markup).toMatch(new RegExp(`<p[^>]*>${label}</p>`))
    expect(markup).not.toContain('<h3')
  })
})
