import type { ReactNode } from 'react'
import type { IconName } from './IconNames'

/** One glyph: its paths on the 16 px grid, and whether it is filled rather than stroked. */
export interface Glyph {
  readonly paths: ReactNode
  /** Drawn in its own fills, not the set's 1.5 px round stroke (`.i`): the play and pause buttons, the theme button. */
  readonly filled?: true
}

const p = (...ds: string[]) => (
  <>
    {ds.map((d) => (
      <path key={d} d={d} />
    ))}
  </>
)

const fileOutline = 'M9 2H4.5A1.5 1.5 0 0 0 3 3.5v9A1.5 1.5 0 0 0 4.5 14h7a1.5 1.5 0 0 0 1.5-1.5V6z'

/**
 * The app's icons, one set (design 1q): every glyph the app draws, promoted
 * here from the module that drew it -- the sidebar's places and buttons, the
 * session's, the Models place's, the media places' -- since the gallery's
 * type page is each one's second user, as `styles` takes any component a
 * second module wants. Each is the old prototype's glyph as the port drew
 * it, on the 16 px grid with the 1.5 px round stroke (`.ap .i`), taking the
 * color of its text. None is drawn new: an icon nobody draws is not here.
 */
export const iconSet: Readonly<Record<IconName, Glyph>> = {
  local: {
    paths: (
      <>
        <rect x="4" y="4" width="8" height="8" rx="1.5" />
        <path d="M6.5 2v2M9.5 2v2M6.5 12v2M9.5 12v2M2 6.5h2M2 9.5h2M12 6.5h2M12 9.5h2" />
      </>
    ),
  },
  api: {
    paths: (
      <>
        <ellipse cx="8" cy="4.5" rx="5" ry="2" />
        <path d="M3 4.5V8c0 1.1 2.2 2 5 2s5-.9 5-2V4.5" />
        <path d="M3 8v3.5c0 1.1 2.2 2 5 2s5-.9 5-2V8" />
      </>
    ),
  },
  subscription: { paths: p('M12.5 6A4.75 4.75 0 0 0 3.6 5.2', 'M3.5 10a4.75 4.75 0 0 0 8.9.8', 'M3.3 2.6v2.8h2.8', 'M12.7 13.4v-2.8H9.9') },
  file: { paths: p(fileOutline, 'M9 2v4h4') },
  folder: { paths: p('M2 4.5A1.5 1.5 0 0 1 3.5 3h2.8l1.5 1.5h4.7A1.5 1.5 0 0 1 14 6v5.5a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 2 11.5z') },
  document: { paths: p(fileOutline, 'M9 2v4h4M5.5 9h5M5.5 11.5h3') },
  image: {
    paths: (
      <>
        <rect x="2.5" y="3" width="11" height="10" rx="1.5" />
        <circle cx="6" cy="6.5" r="1.2" />
        <path d="M13.5 10.5L10 7l-6 6" />
      </>
    ),
  },
  audio: { paths: p('M2.5 8h1M5 5.5v5M7.5 3.5v9M10 6v4M12.5 7v2') },
  video: {
    paths: (
      <>
        <rect x="2" y="3.5" width="9" height="9" rx="1.5" />
        <path d="M11 7l3-2v6l-3-2" />
      </>
    ),
  },
  write: { paths: p('M10.5 3l2.5 2.5L6 12.5H3.5V10z') },
  search: {
    paths: (
      <>
        <circle cx="7" cy="7" r="4.25" />
        <path d="M10.2 10.2L13.5 13.5" />
      </>
    ),
  },
  memory: { paths: p('M2.5 8a5.5 5.5 0 1 0 1.6-3.9', 'M2.5 2.5v2.2h2.2', 'M8 5.5V8l1.8 1.2') },
  private: { paths: p('M2 8s2.2-4 6-4c1 0 1.9.3 2.7.7M14 8s-2.2 4-6 4c-1 0-1.9-.3-2.7-.7', 'M6.6 9.4a2 2 0 0 1 2.8-2.8', 'M3 13L13 3') },
  attach: { paths: p('M13 7.5l-5.2 5.2a3 3 0 0 1-4.3-4.3l5.6-5.6a2 2 0 0 1 2.9 2.9l-5.5 5.5a1 1 0 0 1-1.4-1.4l5-5') },
  error: { paths: p('M8 2.5l6 10.5H2z', 'M8 6.5v3M8 11.2v.1') },
  sessions: { paths: p('M3 4.5A1.5 1.5 0 0 1 4.5 3h7A1.5 1.5 0 0 1 13 4.5v5a1.5 1.5 0 0 1-1.5 1.5H7.5L4.5 13.5V11A1.5 1.5 0 0 1 3 9.5z') },
  models: { paths: p('M8 2l5.5 3v6L8 14l-5.5-3V5z', 'M2.5 5L8 8l5.5-3M8 8v6') },
  suites: { paths: p('M8 2.5l5.5 3L8 8.5l-5.5-3z', 'M2.5 8.5L8 11.5l5.5-3', 'M2.5 11L8 14l5.5-3') },
  tasks: { paths: p('M2.5 4.5l1.2 1.2L6 3.5M2.5 10l1.2 1.2L6 9', 'M8.5 4.8h5M8.5 10.3h5') },
  graph: {
    paths: (
      <>
        <circle cx="4" cy="4" r="1.5" />
        <circle cx="12" cy="6" r="1.5" />
        <circle cx="6" cy="12" r="1.5" />
        <path d="M5.4 4.4l5.2 1.2M4.4 5.4l1.2 5.1M10.9 7.1L7.1 11" />
      </>
    ),
  },
  camera: {
    paths: (
      <>
        <rect x="1.75" y="4" width="9" height="8" rx="1.5" />
        <path d="M10.75 7.2L14.25 5v6l-3.5-2.2" />
      </>
    ),
  },
  voice: { paths: p('M2 7v2M4.75 5v6M7.5 2.5v11M10.25 5v6M13 6.5v3') },
  knowledge: { paths: p('M3 2.5h3v11H3zM7 2.5h3v11H7z', 'M10.6 3.2l2.5.7-2.5 9.4-2.5-.7z') },
  training: { paths: p('M1.5 6L8 3l6.5 3L8 9z', 'M4 7.3v3.4C5.5 12 10.5 12 12 10.7V7.3M14.5 6v4') },
  library: { paths: p('M2.5 3.5h4v4h-4zM9.5 3.5h4v4h-4zM2.5 9.5h4v4h-4z', 'M11.5 9.5v4M9.5 11.5h4') },
  review: {
    paths: (
      <>
        <circle cx="4.5" cy="3.5" r="1.5" />
        <circle cx="4.5" cy="12.5" r="1.5" />
        <circle cx="11.5" cy="12.5" r="1.5" />
        <path d="M4.5 5v6M11.5 11V7a2 2 0 0 0-2-2H7" />
        <path d="M8.5 3.5L7 5l1.5 1.5" />
      </>
    ),
  },
  monitor: { paths: p('M1.5 8.5h3l1.5-4 3 8 1.5-4h4') },
  console: { paths: p('M3 4.5l3.5 3.5L3 11.5M8 12h5') },
  settings: {
    paths: (
      <>
        <circle cx="8" cy="8" r="2.2" />
        <path d="M8 1.8v1.8M8 12.4v1.8M14.2 8h-1.8M3.6 8H1.8M12.4 3.6l-1.3 1.3M4.9 11.1l-1.3 1.3M12.4 12.4l-1.3-1.3M4.9 4.9L3.6 3.6" />
      </>
    ),
  },
  theme: {
    filled: true,
    paths: (
      <>
        <circle cx="8" cy="8" r="5.5" fill="none" stroke="currentColor" strokeWidth="1.5" />
        <path d="M8 2.5a5.5 5.5 0 0 1 0 11z" fill="currentColor" />
      </>
    ),
  },
  add: { paths: p('M8 3.5v9M3.5 8h9') },
  close: { paths: p('M4.5 4.5l7 7M11.5 4.5l-7 7') },
  check: { paths: p('M3.5 8.5l3 3 6-7') },
  more: { paths: <path d="M3.5 8h.1M8 8h.1M12.5 8h.1" strokeWidth="2.4" /> },
  disclosure: { paths: p('M4 6l4 4 4-4') },
  chevronDown: { paths: p('M4.5 6.5L8 10l3.5-3.5') },
  chevronRight: { paths: p('M6.5 4.5L10 8l-3.5 3.5') },
  expandAll: { paths: p('M4.5 3.5L8 7l3.5-3.5M4.5 12.5L8 9l3.5 3.5') },
  collapseAll: { paths: p('M4.5 6.5L8 3l3.5 3.5M4.5 9.5L8 13l3.5-3.5') },
  send: { paths: p('M8 13V3.5M4 7.5l4-4 4 4') },
  inspector: {
    paths: (
      <>
        <rect x="2" y="3" width="12" height="10" rx="2" />
        <path d="M10 3v10" />
      </>
    ),
  },
  newSession: { paths: p('M8 3H4.5A1.5 1.5 0 0 0 3 4.5v7A1.5 1.5 0 0 0 4.5 13h7a1.5 1.5 0 0 0 1.5-1.5V8', 'M11.5 2.5l2 2L8 10H6V8z') },
  execute: { paths: p('M2.5 13.5l6-6', 'M7.5 3.5l3-1.5 3.5 3.5-1.5 3-1.5-1.5-2 2-2.5-2.5 2-2z') },
  held: { paths: p('M2.5 8a5.5 5.5 0 1 0 1.6-3.9', 'M2.5 2.5v2.5H5', 'M8 5v3l2 1.5') },
  download: { paths: p('M8 2.5v8M4.5 7L8 10.5 11.5 7M3 13.5h10') },
  remove: {
    paths: (
      <>
        <circle cx="8" cy="8" r="5.5" />
        <path d="M5.5 8h5" />
      </>
    ),
  },
  reset: { paths: p('M3 8a5 5 0 1 0 1.5-3.5', 'M3 2.5V5h2.5') },
  replan: { paths: p('M13 8a5 5 0 0 1-9 3M3 8a5 5 0 0 1 9-3', 'M12 2.5V5H9.5M4 13.5V11h2.5') },
  drafter: { paths: p('M2.5 12.5c0-2.4 2-4 4.6-4h2.3c2 0 3.6 1.2 3.6 3v1h-1.6', 'M9.3 8.5L7.8 3M11.2 8.6l.6-5.1', 'M2.5 12.5h7') },
  play: { filled: true, paths: <path d="M5 3.2v9.6L13 8z" fill="currentColor" /> },
  pause: { filled: true, paths: <path d="M4.5 3h2.5v10H4.5zM9 3h2.5v10H9z" fill="currentColor" /> },
  picture: {
    paths: (
      <>
        <rect x="2" y="3" width="12" height="10" rx="1.5" />
        <circle cx="6" cy="6.5" r="1.2" />
        <path d="M2.5 12l3.5-3.5 2.5 2.5 2-2 3 3" />
      </>
    ),
  },
  photo: {
    paths: (
      <>
        <rect x="2.5" y="3.5" width="11" height="9" rx="1.5" />
        <path d="M4.5 10.5l2.5-3 2 2 1.5-1.5 1.5 2.5" />
      </>
    ),
  },
  music: {
    paths: (
      <>
        <path d="M6 12.5V3.5l7-1.5v9" />
        <circle cx="4.5" cy="12.5" r="1.5" />
        <circle cx="11.5" cy="11" r="1.5" />
      </>
    ),
  },
  captions: {
    paths: (
      <>
        <rect x="2" y="3.5" width="12" height="9" rx="1.5" />
        <path d="M4.5 9.5h3M9 9.5h2.5M4.5 7h7" />
      </>
    ),
  },
  installing: {
    paths: (
      <>
        <circle cx="8" cy="8" r="5.5" />
        <path d="M8 5v6M5.5 8.5L8 11l2.5-2.5" />
      </>
    ),
  },
  installed: {
    paths: (
      <>
        <circle cx="8" cy="8" r="5.5" />
        <path d="M5.5 8.2l1.8 1.8 3.2-3.6" />
      </>
    ),
  },
  installStopped: {
    paths: (
      <>
        <circle cx="8" cy="8" r="5.5" />
        <rect x="6" y="6" width="4" height="4" rx=".5" />
      </>
    ),
  },
}
