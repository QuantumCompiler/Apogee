import { themes } from '../../styles/Appearance/Appearance'
import { themeIds } from '../../styles/Tokens/Tokens'
import type { ThemeId } from '../../styles/Tokens/Tokens'
import { Observable } from '../../styles/Observation/Observable'
import { boardHeadings, boards, galleryPages } from '../Models/Boards'
import type { BoardEntry, GalleryPage } from '../Models/Boards'

/**
 * How the stand-in finds `apogee` (design 1d), as the gallery offers it on a
 * screen or the App board: the same three words, written here because the
 * gallery imports `styles` alone.
 */
export type ApogeeMode = 'installed' | 'missing' | 'config-typo'

export const apogeeModes: readonly ApogeeMode[] = ['installed', 'missing', 'config-typo']

/** One board's props: its theme, and on the app, how `apogee` is found. */
export interface BoardProps {
  readonly theme: ThemeId
  readonly apogee: ApogeeMode
}

/** What the gallery opens at: a page, and a board focused at full size, in the theme its address names (design 1s). */
export interface GalleryAddress {
  readonly page: GalleryPage
  readonly board?: string
  readonly theme?: ThemeId
}

/** What a frame mounts: the app at a start preset, still, or live at the app's own start (the App board). */
export interface BoardMount {
  readonly start?: string
  readonly still: boolean
  readonly theme: ThemeId
  readonly apogee: ApogeeMode
}

/** The page's address bar, as the gallery reads and writes it: the query, which the page keeps when opened from disk. */
export interface AddressBar {
  readonly search: string
  /** Puts `search` in the address bar in place of the query, adding no history entry. */
  replace(search: string): void
}

/**
 * The gallery's own words in the address: `gallery`, its page, `board`, which
 * also opens 1r's Build pages (`&board=BuildContract`), and `theme`, the
 * focused board's theme (design 1s: so each of a board's old pictures has an
 * address to pair with), written only when it is not dark, the default.
 */
const galleryParameter = 'gallery'
const boardParameter = 'board'
const themeParameter = 'theme'
const galleryWords: readonly string[] = [galleryParameter, boardParameter, themeParameter]

/** The query's parameters, in their order, a bare word (`?gallery`) as an empty value. */
function parameters(search: string): Array<[string, string]> {
  return [...new URLSearchParams(search.replace(/^\?/, ''))]
}

/** A query from its parameters: a bare word for an empty value, a board's colon kept (`board=settings:keyboard`). */
function query(parts: ReadonlyArray<readonly [string, string]>): string {
  if (parts.length === 0) return ''
  const encode = (text: string) => encodeURIComponent(text).replace(/%3A/g, ':')
  return `?${parts.map(([name, value]) => (value === '' ? encode(name) : `${encode(name)}=${encode(value)}`)).join('&')}`
}

/**
 * Whether the page at `search` asks for the gallery, and where it opens
 * (design 1q): `?gallery` the Build page, `?gallery=foundations` the
 * Foundations page, and `&board=<id>` a board at full size, on its own page
 * whatever the page said, in the theme `&theme=<id>` names (design 1s). An
 * id or a theme the gallery lacks is passed over, and so is a theme with no
 * board. `undefined` with no `gallery` word: the app opens. `?page=` is never
 * read: 1r's Build pages open as boards, `&board=BuildContract`.
 */
export function galleryAddress(search: string): GalleryAddress | undefined {
  const said = new Map(parameters(search))
  const page = said.get(galleryParameter)
  if (page === undefined) return undefined
  const board = boards.find((entry) => entry.id === said.get(boardParameter))
  const theme = themeIds.find((id) => id === said.get(themeParameter))
  if (board) return theme === undefined ? { page: board.page, board: board.id } : { page: board.page, board: board.id, theme }
  return { page: page === 'foundations' ? 'foundations' : 'build' }
}

/** The address that opens the gallery from the app's: `gallery` added (its Build page), every other word kept in its place. */
export function withGallery(search: string): string {
  return query([...parameters(search).filter(([name]) => !galleryWords.includes(name)), [galleryParameter, '']])
}

/** The app's own address once the gallery is left: the gallery's words taken out, every other word kept in its place. */
export function withoutGallery(search: string): string {
  return query(parameters(search).filter(([name]) => !galleryWords.includes(name)))
}

/** One row of a page: the heading over it, and its boards in table order. */
export interface BoardRow {
  readonly heading: string
  readonly boards: readonly BoardEntry[]
}

/** The prop switcher's choices for a board: its theme, and on the app its `apogee` mode. */
export interface SwitcherFace {
  readonly themes: ReadonlyArray<{ readonly id: ThemeId; readonly name: string; readonly chosen: boolean }>
  readonly modes?: ReadonlyArray<{ readonly id: ApogeeMode; readonly chosen: boolean }>
}

const defaultProps: BoardProps = { theme: 'dark', apogee: 'installed' }

/**
 * The gallery (design 1q): the design's review surface, the canvas's job
 * now that the canvas retires (1t) -- not a product screen, so it is in no
 * place, key map or Settings pane. It holds the page open, the board focused
 * at full size, each board's props, the frames scrolled near enough to be
 * mounted, and the address it reads and writes: `?gallery`,
 * `?gallery=foundations`, `&board=<id>`. Plain TypeScript: the view draws
 * it, and `app` hands the view the app to mount.
 */
export class GalleryViewModel extends Observable {
  private shown: GalleryPage
  private focusedId: string | undefined
  private readonly chosen = new Map<string, BoardProps>()
  private readonly mounted = new Set<string>()

  constructor(
    private readonly address: AddressBar,
    readonly entries: readonly BoardEntry[] = boards,
  ) {
    super()
    const opened: GalleryAddress = galleryAddress(address.search) ?? { page: 'build' }
    this.shown = opened.page
    this.focusedId = opened.board
    if (opened.board !== undefined && opened.theme !== undefined) this.chosen.set(opened.board, { ...defaultProps, theme: opened.theme })
  }

  /** The page open: Build or Foundations. */
  get page(): GalleryPage {
    return this.shown
  }

  /** The pages, as the tabs name them. */
  get pages(): typeof galleryPages {
    return galleryPages
  }

  /** The board open at full size, if one is. */
  get focused(): BoardEntry | undefined {
    return this.entries.find((entry) => entry.id === this.focusedId)
  }

  /** Opens a page by its tab. */
  open(page: GalleryPage): void {
    if (page === this.shown && this.focusedId === undefined) return
    this.shown = page
    this.focusedId = undefined
    this.write()
    this.changed()
  }

  /** Opens a board at full size: clicking its frame. */
  focus(id: string): void {
    const entry = this.entries.find((each) => each.id === id)
    if (!entry) return
    this.shown = entry.page
    this.focusedId = id
    this.write()
    this.changed()
  }

  /** Esc: back to the rows from a board at full size. Whether it did anything. */
  escape(): boolean {
    if (this.focusedId === undefined) return false
    this.focusedId = undefined
    this.write()
    this.changed()
    return true
  }

  /** A page's rows, each under its heading, its boards in table order. */
  rows(page: GalleryPage): BoardRow[] {
    const onPage = this.entries.filter((entry) => entry.page === page)
    const numbers = [...new Set(onPage.map((entry) => entry.row))].sort((a, b) => a - b)
    return numbers.map((row) => ({
      heading: boardHeadings.find((heading) => heading.page === page && heading.row === row)?.text ?? '',
      boards: onPage.filter((entry) => entry.row === row),
    }))
  }

  /** A board's props: dark and `installed` until the switcher changes them. */
  props(id: string): BoardProps {
    return this.chosen.get(id) ?? defaultProps
  }

  setTheme(id: string, theme: ThemeId): void {
    this.choose(id, { ...this.props(id), theme })
  }

  setApogee(id: string, apogee: ApogeeMode): void {
    this.choose(id, { ...this.props(id), apogee })
  }

  /** The switcher's choices: the three themes on every board, and the `apogee` mode on the screens and the App board. */
  switcher(id: string): SwitcherFace {
    const props = this.props(id)
    const entry = this.entries.find((each) => each.id === id)
    const names = themes()
    const face: SwitcherFace = { themes: themeIds.map((theme) => ({ id: theme, name: names[theme].name, chosen: theme === props.theme })) }
    if (entry?.draws !== 'screen' && entry?.draws !== 'app') return face
    return { ...face, modes: apogeeModes.map((mode) => ({ id: mode, chosen: mode === props.apogee })) }
  }

  /** What a frame mounts: a screen its preset, still, at its props; the App board live, at the app's own start; `undefined` for a page that is not the app. */
  frame(id: string): BoardMount | undefined {
    const entry = this.entries.find((each) => each.id === id)
    const props = this.props(id)
    if (entry?.draws === 'screen' && entry.start !== undefined) return { start: entry.start, still: true, theme: props.theme, apogee: props.apogee }
    if (entry?.draws === 'app') return { still: false, theme: props.theme, apogee: props.apogee }
    return undefined
  }

  /** Whether a frame is mounted: scrolled near enough, or focused, so 90-odd app roots never run at once. */
  isMounted(id: string): boolean {
    return this.mounted.has(id) || this.focusedId === id
  }

  /** A frame scrolled into view (`true`), or far off screen (`false`), as the view sees it. */
  near(id: string, near: boolean): void {
    if (near === this.mounted.has(id)) return
    if (near) this.mounted.add(id)
    else this.mounted.delete(id)
    this.changed()
  }

  private choose(id: string, props: BoardProps): void {
    const theme = this.props(id).theme
    this.chosen.set(id, props)
    if (id === this.focusedId && props.theme !== theme) this.write()
    this.changed()
  }

  /** The address for the page and the board shown, in its theme where that is not dark (design 1s), the page's other words kept. */
  private write(): void {
    const kept = parameters(this.address.search).filter(([name]) => !galleryWords.includes(name))
    const gallery: [string, string] = [galleryParameter, this.shown === 'build' ? '' : this.shown]
    const focused = this.focusedId
    const theme = focused === undefined ? defaultProps.theme : this.props(focused).theme
    const board: Array<[string, string]> = focused === undefined ? [] : [[boardParameter, focused]]
    const themed: Array<[string, string]> = theme === defaultProps.theme ? [] : [[themeParameter, theme]]
    this.address.replace(query([...kept, gallery, ...board, ...themed]))
  }
}
