import { useEffect, useLayoutEffect, useMemo, useRef } from 'react'
import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { Specimen } from '../../styles/Specimens/Specimen'
import { boardWidth } from '../Models/Boards'
import type { BoardEntry } from '../Models/Boards'
import { BuildContractViewModel } from '../ViewModels/BuildContractViewModel'
import { BuildLayoutViewModel } from '../ViewModels/BuildLayoutViewModel'
import type { KeyLine } from '../ViewModels/BuildLayoutViewModel'
import { BuildProcessesViewModel } from '../ViewModels/BuildProcessesViewModel'
import { colorPage } from '../ViewModels/ColorPage'
import type { BoardMount, GalleryViewModel } from '../ViewModels/GalleryViewModel'
import { typePage } from '../ViewModels/TypePage'
import { BuildContractPage } from './BuildContractPage'
import { BuildLayoutPage } from './BuildLayoutPage'
import { BuildProcessesPage } from './BuildProcessesPage'
import { ColorTokensPage } from './ColorTokensPage'
import { ComponentsPage } from './ComponentsPage'
import { LogoPage } from './LogoPage'
import { PropSwitcher } from './PropSwitcher'
import { TypeSpacePage } from './TypeSpacePage'

/** How a frame is mounted: its size on the canvas, and whether it takes the keys and the pointer (the App board, open at full size). */
export interface MountSize {
  readonly width: number
  readonly height: number
  readonly live: boolean
}

/** The app mounted in a frame: `app` hands it in, so the gallery never imports `app`, the shell or a place. */
export type MountApp = (mount: BoardMount, size: MountSize) => ReactNode

/** What `app` hands the gallery: the app to mount, the modules' specimens, the logo the build inlined, and the key map the layout page draws (1r). */
export interface GalleryParts {
  readonly mount: MountApp
  readonly specimens: readonly Specimen[]
  readonly logo: string
  /** The shell's key map, as Settings › Keyboard lists it: the gallery never imports the shell. */
  readonly keys: readonly KeyLine[]
}

/** The Build pages (1r): documents, not the app, so at full size they take the pointer -- a gap chip is a link -- where a screen stands inert. */
const buildPages: ReadonlySet<BoardEntry['draws']> = new Set(['contract', 'layout', 'processes'])

/** A frame in a row is a quarter of its board: 360 px wide. */
const rowScale = 0.25

/** A frame in a row shows its board's first 900 px, the canvas's screen frame; open at full size it shows the whole board. */
const rowHeight = 900

/** A page a board draws: a Build page over its data (1r), or a Foundations page generated from the tokens and the modules. */
function BoardPage({ entry, parts }: { readonly entry: BoardEntry; readonly parts: GalleryParts }): ReactNode {
  switch (entry.draws) {
    case 'contract':
      return <BuildContractPage model={new BuildContractViewModel()} />
    case 'layout':
      return <BuildLayoutPage model={new BuildLayoutViewModel(parts.keys)} />
    case 'processes':
      return <BuildProcessesPage model={new BuildProcessesViewModel()} />
    case 'colors':
      return <ColorTokensPage face={colorPage()} />
    case 'type':
      return <TypeSpacePage face={typePage()} />
    case 'components':
      return <ComponentsPage entry={entry} specimens={parts.specimens} />
    case 'logo':
      return <LogoPage logo={parts.logo} />
    case 'screen':
    case 'app':
      return null
  }
}

/** What a board draws, at its props: the app at its preset, or a page over its data, the tokens and the modules. */
function BoardBody({ entry, model, parts, live }: { readonly entry: BoardEntry; readonly model: GalleryViewModel; readonly parts: GalleryParts; readonly live: boolean }) {
  const page = useMemo(() => <BoardPage entry={entry} parts={parts} />, [entry, parts])
  const mount = model.frame(entry.id)
  if (mount) return <>{parts.mount(mount, { width: boardWidth, height: entry.height, live: live && entry.draws === 'app' })}</>
  const theme = model.props(entry.id).theme
  return (
    <div className="ap" data-theme={theme} style={{ minHeight: entry.height, background: 'var(--surface-window)' }}>
      {page}
    </div>
  )
}

/**
 * A board in its row (design 1q): its title, and its frame a quarter of its
 * size. The frame mounts once it is scrolled near and unmounts far off
 * screen, so 90-odd app roots never run at once; it takes no key and no
 * pointer of its own -- clicking it, or its title, opens the board at full
 * size.
 */
export function BoardCard({ entry, model, parts }: { readonly entry: BoardEntry; readonly model: GalleryViewModel; readonly parts: GalleryParts }) {
  useObserved(model)
  const card = useRef<HTMLDivElement>(null)
  useEffect(() => {
    const element = card.current
    if (!element || typeof IntersectionObserver === 'undefined') return undefined
    const near = new IntersectionObserver((seen) => seen.forEach((each) => each.isIntersecting && model.near(entry.id, true)), { rootMargin: '200px' })
    const far = new IntersectionObserver((seen) => seen.forEach((each) => !each.isIntersecting && model.near(entry.id, false)), { rootMargin: '2000px' })
    near.observe(element)
    far.observe(element)
    return () => {
      near.disconnect()
      far.disconnect()
      model.near(entry.id, false)
    }
  }, [model, entry.id])
  const height = Math.min(entry.height, rowHeight)
  return (
    <div ref={card} data-board={entry.id} style={{ width: boardWidth * rowScale, display: 'flex', flexDirection: 'column', gap: 6 }}>
      <button type="button" className="hv" onClick={() => model.focus(entry.id)} title="Open it at full size" style={{ display: 'flex', alignItems: 'baseline', gap: 6, padding: '2px 4px', border: 0, borderRadius: 'var(--radius-row)', color: 'var(--text-secondary)', fontSize: 'var(--t-dense)', textAlign: 'left' }}>
        <span style={{ flex: '1 1 auto', minWidth: 0 }}>{entry.title}</span>
        {entry.noOldBoard ? (
          <span className="mono" style={{ flex: '0 0 auto', fontSize: 'var(--t-tag)', color: 'var(--status-warning)' }}>
            NO OLD BOARD
          </span>
        ) : null}
      </button>
      <div onClick={() => model.focus(entry.id)} aria-hidden="true" style={{ position: 'relative', width: boardWidth * rowScale, height: height * rowScale, overflow: 'hidden', borderRadius: 'var(--radius-card)', border: '1px solid var(--line-border)', background: 'var(--surface-sidebar)', cursor: 'pointer' }}>
        <div inert style={{ width: boardWidth, height, overflow: 'hidden', transform: `scale(${rowScale})`, transformOrigin: '0 0', pointerEvents: 'none' }}>
          {model.isMounted(entry.id) ? <BoardBody entry={entry} model={model} parts={parts} live={false} /> : null}
        </div>
      </div>
    </div>
  )
}

/**
 * One board at full size (design 1q), as the canvas's focused launch: its
 * title, its prop switcher, and the board 1440 px wide. The App board runs
 * live and takes the keys; a screen stands still, inert, and so does a
 * Foundations page; a Build page (1r) takes the pointer, its gap chips being
 * links. Esc, or Back, goes back to the rows.
 */
export function FocusedBoard({ entry, model, parts }: { readonly entry: BoardEntry; readonly model: GalleryViewModel; readonly parts: GalleryParts }) {
  useObserved(model)
  const live = entry.draws === 'app'
  return (
    <section aria-label={entry.title} style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
      <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 16 }}>
        <button type="button" className="btn" onClick={() => model.escape()}>
          Back to the rows <span className="mono" style={{ color: 'var(--text-tertiary)', fontSize: 'var(--t-meta)' }}>esc</span>
        </button>
        <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{entry.title}</h2>
        {entry.noOldBoard ? (
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--status-warning)' }}>
            no old board: the design's frozen prototype drew this preset on no board
          </span>
        ) : null}
        <PropSwitcher model={model} id={entry.id} />
      </div>
      <div style={{ overflowX: 'auto' }}>
        <div
          {...(live ? { 'data-gallery-live': '' } : buildPages.has(entry.draws) ? {} : { inert: true })}
          style={{ width: boardWidth, minHeight: entry.height, ...(entry.draws === 'screen' || live ? { height: entry.height } : {}), position: 'relative', border: '1px solid var(--line-border)', transform: 'translate(0, 0)' }}
        >
          <BoardBody entry={entry} model={model} parts={parts} live={live} />
        </div>
      </div>
    </section>
  )
}

/**
 * The gallery (design 1q): the design's review surface, the canvas's pages
 * and rows inside the built file -- not a product screen, so it is drawn
 * outside the app's frame, and opened by `?gallery` or its key, which `app`
 * owns. Its two tabs, Build and Foundations; on each, the canvas's headings
 * over their rows of boards in table order; a board clicked opens at full
 * size.
 */
export function GalleryView({ model, parts }: { readonly model: GalleryViewModel; readonly parts: GalleryParts }) {
  useObserved(model)
  const focused = model.focused
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return
      // A key pressed inside the live App board is the app's.
      if (event.target instanceof Element && event.target.closest('[data-gallery-live]')) return
      if (model.escape()) event.preventDefault()
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [model])
  // A board opens at the top of the page; Back, or Esc, brings its card back into view in its row (the page's scroll, the view's alone).
  const opened = useRef<string | undefined>(undefined)
  useLayoutEffect(() => {
    if (focused) {
      opened.current = focused.id
      window.scrollTo(0, 0)
      return
    }
    const id = opened.current
    opened.current = undefined
    if (id) document.querySelector(`[data-board="${id}"]`)?.scrollIntoView?.({ block: 'center' })
  }, [focused])
  return (
    <div className="ap" data-theme="dark" style={{ minHeight: '100vh', boxSizing: 'border-box', padding: '24px 32px 64px', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 }}>
      <header style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 20, marginBottom: 24 }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-h1)', fontWeight: 600 }}>Gallery</h1>
        <div role="tablist" aria-label="Pages" className="seg">
          {model.pages.map((page) => (
            <button
              key={page.id}
              type="button"
              role="tab"
              id={`gallery-tab-${page.id}`}
              aria-selected={model.page === page.id}
              aria-controls="gallery-page"
              onClick={() => model.open(page.id)}
              style={{ background: model.page === page.id ? 'var(--surface-control)' : 'transparent', color: model.page === page.id ? 'var(--text-primary)' : 'var(--text-secondary)', fontWeight: model.page === page.id ? 600 : 400 }}
            >
              {page.name}
            </button>
          ))}
        </div>
        <p style={{ margin: 0, flex: '1 1 320px', color: 'var(--text-secondary)' }}>The design’s boards. Each screen is the app at its start preset, standing still; App runs live. Click a board to open it at full size.</p>
      </header>
      <div role="tabpanel" id="gallery-page" aria-labelledby={`gallery-tab-${model.page}`}>
        {focused ? (
          <FocusedBoard entry={focused} model={model} parts={parts} />
        ) : (
          model.rows(model.page).map((row) => (
            <section key={row.heading} aria-label={row.heading} style={{ marginBottom: 40 }}>
              <h2 style={{ margin: '0 0 14px', fontSize: 'var(--t-h2)', fontWeight: 600 }}>{row.heading}</h2>
              <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'flex-start', gap: 20 }}>
                {row.boards.map((entry) => (
                  <BoardCard key={entry.id} entry={entry} model={model} parts={parts} />
                ))}
              </div>
            </section>
          ))
        )}
      </div>
    </div>
  )
}
