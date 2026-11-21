import { describe, expect, test } from 'vitest'
import { boards } from '../../../source/presentation/gallery/Models/Boards'
import { GalleryViewModel, galleryAddress, withoutGallery } from '../../../source/presentation/gallery/ViewModels/GalleryViewModel'
import type { AddressBar } from '../../../source/presentation/gallery/ViewModels/GalleryViewModel'

/** An address bar that keeps what the gallery writes. */
function bar(search: string): AddressBar & { readonly written: string[] } {
  const written: string[] = []
  return {
    written,
    get search() {
      return written.at(-1) ?? search
    },
    replace(next: string) {
      written.push(next)
    },
  }
}

describe('the address the gallery reads: ?gallery, ?gallery=foundations and &board=<id>, never ?page=', () => {
  test('?gallery is the Build page, ?gallery=foundations the Foundations page', () => {
    expect(galleryAddress('?gallery')).toEqual({ page: 'build' })
    expect(galleryAddress('?gallery=')).toEqual({ page: 'build' })
    expect(galleryAddress('?gallery=build')).toEqual({ page: 'build' })
    expect(galleryAddress('?gallery=foundations')).toEqual({ page: 'foundations' })
  })

  test('&board=<id> focuses a board, on its own page whatever the page said; an id the table lacks is passed over', () => {
    expect(galleryAddress('?gallery&board=ScreenModels')).toEqual({ page: 'build', board: 'ScreenModels' })
    expect(galleryAddress('?gallery&board=ColorTokens')).toEqual({ page: 'foundations', board: 'ColorTokens' })
    expect(galleryAddress('?gallery=foundations&board=chat:compacting')).toEqual({ page: 'build', board: 'chat:compacting' })
    expect(galleryAddress('?gallery&board=Nope')).toEqual({ page: 'build' })
  })

  test('no gallery without ?gallery: the app opens; ?page= is never the gallery’s (1r’s pages took &board=)', () => {
    expect(galleryAddress('')).toBeUndefined()
    expect(galleryAddress('?start=models&still=true')).toBeUndefined()
    expect(galleryAddress('?page=foundations')).toBeUndefined()
    expect(galleryAddress('?gallery&page=foundations')).toEqual({ page: 'build' })
  })

  test('the app’s own address once the gallery is left: its words gone, the rest kept', () => {
    expect(withoutGallery('?gallery=foundations&board=ColorTokens')).toBe('')
    expect(withoutGallery('?start=models&gallery&board=ScreenModels&still=true')).toBe('?start=models&still=true')
  })
})

describe('the gallery view-model', () => {
  test('opens on the address’s page; a tab writes its address', () => {
    const address = bar('?gallery')
    const gallery = new GalleryViewModel(address)
    expect(gallery.page).toBe('build')
    gallery.open('foundations')
    expect([gallery.page, address.search]).toEqual(['foundations', '?gallery=foundations'])
    gallery.open('build')
    expect([gallery.page, address.search]).toEqual(['build', '?gallery'])
  })

  test('a board focused at full size writes &board=<id>; Esc goes back to the rows and the address with it', () => {
    const address = bar('?start=chat&gallery')
    const gallery = new GalleryViewModel(address)
    gallery.focus('ScreenModels')
    expect(gallery.focused?.title).toBe('Models · backends, one open')
    expect(address.search).toBe('?start=chat&gallery&board=ScreenModels')
    expect(gallery.escape()).toBe(true)
    expect(gallery.focused).toBeUndefined()
    expect(address.search).toBe('?start=chat&gallery')
    expect(gallery.escape()).toBe(false)
  })

  test('opened at a focused board: its page and the board', () => {
    const gallery = new GalleryViewModel(bar('?gallery&board=Logo'))
    expect([gallery.page, gallery.focused?.id]).toEqual(['foundations', 'Logo'])
  })

  test("a board-less preset's id keeps its colon in the address", () => {
    const address = bar('?gallery')
    new GalleryViewModel(address).focus('settings:keyboard')
    expect(address.search).toBe('?gallery&board=settings:keyboard')
  })

  test('the rows: each page’s headings over its rows, the boards in table order', () => {
    const gallery = new GalleryViewModel(bar('?gallery'))
    const build = gallery.rows('build')
    expect(build.map((row) => row.heading)).toEqual([
      'Build — the contract, the measurements, and the app itself',
      'Screens — sessions, the system monitor and the console',
      'Screens — models, suites, tasks, the code graph, knowledge, training, the library and review',
      'Screens — video and voice',
      'Screens — settings',
    ])
    expect(build[0]?.boards.map((entry) => entry.id)).toEqual(['BuildContract', 'BuildLayout', 'App', 'BuildProcesses'])
    expect(build.flatMap((row) => row.boards)).toEqual(boards.filter((entry) => entry.page === 'build'))
    const foundations = gallery.rows('foundations')
    expect(foundations.map((row) => [row.heading, row.boards.length])).toEqual([['Foundations — color, type, space and components', 7]])
  })

  test('each board’s props: the theme on every board, the apogee mode on the screens and the App board only', () => {
    const gallery = new GalleryViewModel(bar('?gallery'))
    expect(gallery.props('ScreenModels')).toEqual({ theme: 'dark', apogee: 'installed' })
    gallery.setTheme('ScreenModels', 'light')
    gallery.setApogee('ScreenModels', 'config-typo')
    expect(gallery.props('ScreenModels')).toEqual({ theme: 'light', apogee: 'config-typo' })
    expect(gallery.props('ScreenModelsStore')).toEqual({ theme: 'dark', apogee: 'installed' })
    const screen = gallery.switcher('ScreenModels')
    expect(screen.themes.map((theme) => [theme.id, theme.name, theme.chosen])).toEqual([
      ['dark', 'Graphite Dark', false],
      ['light', 'Graphite Light', true],
      ['contrast', 'High Contrast', false],
    ])
    expect(screen.modes?.map((mode) => [mode.id, mode.chosen])).toEqual([
      ['installed', false],
      ['missing', false],
      ['config-typo', true],
    ])
    expect(gallery.switcher('App').modes).toHaveLength(3)
    expect(gallery.switcher('ColorTokens').modes).toBeUndefined()
    expect(gallery.switcher('ComponentsTurn').themes).toHaveLength(3)
  })

  test("what each frame mounts: a screen its preset, still, at its props; the App board live at the app's own start", () => {
    const gallery = new GalleryViewModel(bar('?gallery'))
    gallery.setApogee('ScreenSessionError', 'missing')
    expect(gallery.frame('ScreenSessionError')).toEqual({ start: 'chat:crash', still: true, theme: 'dark', apogee: 'missing' })
    expect(gallery.frame('settings:keyboard')).toEqual({ start: 'settings:keyboard', still: true, theme: 'dark', apogee: 'installed' })
    gallery.setTheme('App', 'contrast')
    expect(gallery.frame('App')).toEqual({ still: false, theme: 'contrast', apogee: 'installed' })
    expect(gallery.frame('ColorTokens')).toBeUndefined()
  })

  test('frames mount when scrolled near and unmount far off screen; the focused board is always mounted', () => {
    const gallery = new GalleryViewModel(bar('?gallery'))
    expect(gallery.isMounted('ScreenModels')).toBe(false)
    gallery.near('ScreenModels', true)
    expect(gallery.isMounted('ScreenModels')).toBe(true)
    gallery.near('ScreenModels', false)
    expect(gallery.isMounted('ScreenModels')).toBe(false)
    gallery.focus('ScreenModels')
    expect(gallery.isMounted('ScreenModels')).toBe(true)
  })

  test('each change is told to the view', () => {
    const gallery = new GalleryViewModel(bar('?gallery'))
    let told = 0
    gallery.subscribe(() => (told += 1))
    gallery.open('foundations')
    gallery.focus('Logo')
    gallery.setTheme('Logo', 'light')
    gallery.near('ScreenModels', true)
    gallery.near('ScreenModels', true)
    gallery.escape()
    expect(told).toBe(5)
  })
})
