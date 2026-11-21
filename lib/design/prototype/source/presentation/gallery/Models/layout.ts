import type { ColorName } from '../../styles/Tokens/Tokens'

/**
 * The layout page's data (design 1r): the old BuildLayout board, "layout,
 * navigation and native controls", at 1c's frozen revision `b375fac9`,
 * converted once and kept here as data. Its words are the board's; every
 * size and key in them is named, never written: a `{kind:name}` the page's
 * view-model reads from the tokens or the key map --
 *
 * - `{layout:sidebar}`, a layout size by its path in tokens.json's
 *   `layoutPx` (`{layout:settings.width}`); `{layout:a|b}` sizes that are
 *   drawn as one (`dialog.download|dialog.install`), said once when they
 *   agree;
 * - `{control:…}`, `{radius:…}`: a control height, a radius;
 * - `{motion:chevron}`: a motion's duration (`150 ms`);
 * - `{spacing:step}`, `{spacing:all}`: the spacing's first step, all of it;
 * - `{scrim:percent}`: how dark the modal scrim is (`45%`);
 * - `{key:Search sessions}`: a line of the key map, by its words, drawn as
 *   its keys (`⇧⌘F`).
 *
 * So a size or a key is edited in tokens.json or the key map, and this page
 * follows. Only three numbers are words, as the board writes them: its place
 * among the Build boards, a read's 300 ms and the 4.5:1 contrast floor.
 */

/** The page's own words. */
export const layoutWords = {
  eyebrow: 'BUILD · 2 OF 2',
  title: 'Layout, navigation and native controls',
  intro:
    'The measurements behind every screen, how a person moves between them, the states each one draws, and what each piece of the design becomes in SwiftUI first, then Qt Quick and WinUI. The numbers are the prototype’s own; with the tokens they are in `handoff/tokens.json` in this canvas.',
  window: 'The window',
  windowIntro: 'At three-quarter scale. Every screen keeps the sidebar and the header; what sits under the header is the screen’s own.',
  regions: 'Regions and sizes',
  regionsIntro: 'In points. Panes are fixed widths; content takes the rest.',
  keys: 'Navigation and keys',
  keysIntro: 'Every destination and overlay has a key, and nothing needs the pointer. Space and {key:Generate, Speak, Plan or Render, from its form} are new in the media screens.',
  states: 'States every screen draws',
  statesIntro: 'The prototype shows the full screens; these are the edges each one also needs.',
  controls: 'Native controls',
  controlsIntro: 'What each piece becomes on each platform. The look comes from the tokens, not from the platform’s default styling, except for window controls, menus’ keyboard handling and the colour picker.',
  type: 'Type',
  typeIntro: 'IBM Plex Sans and Mono by default; the person can choose the system fonts or Atkinson Hyperlegible, and Small, Medium, Large or Extra large, which scale every size and round to the half point. Shown at Medium.',
  space: 'Space, shape and motion',
} as const

/** A region of the window: its name, its size and what it is for. */
export interface Region {
  readonly name: string
  readonly size: string
  readonly words: string
}

/** Regions and sizes, in the board's order and words. */
export const regions: readonly Region[] = [
  { name: 'Window', size: 'at least {layout:windowMin.width} × {layout:windowMin.height}', words: 'Below {layout:collapseBelow} wide the sidebar becomes a {layout:sidebarCollapsed} px column of icons, and side panes close.' },
  { name: 'Sidebar', size: '{layout:sidebar}', words: 'Sessions by date, places, system, settings. Every group remembers whether it is open.' },
  { name: 'Header', size: '{layout:header}', words: 'The screen’s title and its own controls. In a session: model, thinking, context, inspector.' },
  { name: 'Inspector', size: '{layout:inspector}', words: 'Sessions only. Closing it gives the conversation the room.' },
  { name: 'Detail panes', size: '{layout:modelDetail} · {layout:clipForm} · {layout:compositionPane} · {layout:graphDetail}', words: 'A model’s details · generating a clip · a composition’s changes · a code symbol.' },
  { name: 'List columns', size: '{layout:taskList} · {layout:suiteList} · {layout:mediaList} · {layout:graphFiles}', words: 'Tasks · suites · clips, compositions and speeches · the code graph’s files.' },
  { name: 'Content widths', size: '{layout:conversationMax} · {layout:mediaContentMax}', words: 'The conversation, compositions and tasks · clips, voice and roles.' },
  { name: 'Settings', size: '{layout:settings.width} × {layout:settings.height}, nav {layout:settings.nav}', words: 'A sheet over the window; changes apply as they are made.' },
  { name: 'Dialogs', size: '{layout:dialog.newTask} · {layout:dialog.download|dialog.install} · {layout:dialog.confirm}', words: 'New task · download, install · confirm.' },
  { name: 'Menus', size: '{layout:menu.thinking} · {layout:menu.switchModel}', words: 'Thinking · switch model.' },
  { name: 'Toast', size: 'bottom centre', words: 'One line, gone after about five seconds, never the only place something is said.' },
]

/** A row of Navigation and keys: its words, and the key map's lines it shows, by their words, drawn as their keys. */
export interface KeyRow {
  readonly words: string
  readonly lines: readonly string[]
}

/** Navigation and keys: the board's 13 grouped rows, in its words; together they name every line of the key map. */
export const keyRows: readonly KeyRow[] = [
  { words: 'Sessions, Models, Suites, Tasks, Code graph, Video, Voice, Knowledge, Training, Library', lines: ['Sessions, Models, Suites, Tasks, Code graph, Video, Voice, Knowledge, Training, Library'] },
  { words: 'Review, the eleventh place', lines: ['Review'] },
  { words: 'New session · new private session · new execute session', lines: ['New session', 'New private session', 'New execute session'] },
  { words: 'Search sessions', lines: ['Search sessions'] },
  { words: 'Switch model · models library', lines: ['Switch model', 'Models library'] },
  { words: 'Inspector · system monitor · settings', lines: ['Show or hide the inspector', 'System monitor', 'Settings'] },
  { words: 'The console: any command, as typed', lines: ['Console'] },
  { words: 'Send · new line · commands · mention a file', lines: ['Send', 'New line', 'Commands', 'Mention a file'] },
  { words: 'Stop what is running here', lines: ['Stop the answer'] },
  { words: 'On a permission card: allow once · deny · pick', lines: ['Allow once on a permission card', 'Deny on a permission card', 'Pick an answer on a card'] },
  { words: 'Play or pause the focused player', lines: ['Play or pause the focused player'] },
  { words: 'Generate, Speak, Plan or Render, from its form', lines: ['Generate, Speak, Plan or Render, from its form'] },
  { words: 'Hide the sidebar', lines: ['Hide the sidebar'] },
]

/** A state every screen draws: its name and what it shows. */
export interface ScreenState {
  readonly name: string
  readonly words: string
}

/** States every screen draws, in the board's order and words. */
export const screenStates: readonly ScreenState[] = [
  { name: 'Reading', words: 'The screen’s shape with quiet placeholders. No spinner for a read under 300 ms.' },
  { name: 'Empty', words: 'One sentence saying what goes here, and the one action that fills it.' },
  { name: 'A command failed', words: 'Its stderr’s last line, word for word, and the exit code. When a refusal names the command that fixes it, that command is the button.' },
  { name: 'The CLI is missing or too old', words: 'One screen saying which build is needed, with the installer’s link.' },
  { name: 'Something is running', words: 'A dot on its place, a pulse on its row, and Stop wherever it shows.' },
  { name: 'Cloud, billed', words: 'The model-API colour, an icon and the word “billed” wherever a cloud API answers or plans. Colour is never the only sign.' },
]

/** Space, shape and motion, in the board's order and words. */
export const spaceRows: readonly ScreenState[] = [
  { name: 'Spacing', words: 'A {spacing:step} pt step: {spacing:all}.' },
  { name: 'Control heights', words: '{control:small} small · {control:button|row} buttons, fields, rows · {control:toolbarButton} header controls · {control:listRow} list rows · {control:header} headers.' },
  { name: 'Radii', words: '{radius:tag} tags · {radius:chip} chips · {radius:row} rows · {radius:control} controls · {radius:segment} segmented · {radius:card} cards · {radius:menu} menus · {radius:dialog} dialogs.' },
  { name: 'Motion', words: 'Chevrons turn in {motion:chevron}. Live things pulse at {motion:pulse}; the caret blinks at {motion:caret}. With Reduce Motion both stop.' },
  { name: 'Focus', words: 'A {layout:focusRing.width} pt ring in the accent, {layout:focusRing.offset} pt out, on every control.' },
  { name: 'Contrast', words: 'Text 4.5:1 against what it sits on, checked live in Settings › Appearance as colours change.' },
  { name: 'Not tokens', words: 'The sheet’s scrim (black at {scrim:percent}) and the menu and dialog shadows; window controls are the platform’s.' },
]

/** A part of the window the diagram draws. */
export type WindowPart = 'window' | 'sidebar' | 'sessions' | 'places' | 'system' | 'settings' | 'header' | 'content' | 'inspector' | 'composer'

/** A part's label, its note under it, and the colors it is drawn in. */
export interface WindowLabel {
  readonly title: string
  readonly note?: string
  readonly surface: ColorName
  readonly line: ColorName
}

/** The window diagram's parts, in the order they are drawn, each in the board's words. */
export const windowLabels: Readonly<Record<WindowPart, WindowLabel>> = {
  window: { title: '', surface: 'surface.window', line: 'line.strong' },
  sidebar: { title: 'Sidebar · {layout:sidebar}', surface: 'surface.sidebar', line: 'line.border' },
  sessions: { title: 'Sessions', note: 'grouped by date, each group collapsible · search {key:Search sessions}', surface: 'surface.sidebar', line: 'line.border' },
  places: { title: 'Places · {layout:place.width} × {layout:place.height} each', note: 'scrolls sideways · a dot when something runs', surface: 'surface.sidebar', line: 'line.border' },
  system: { title: 'System', note: 'collapsible · cpu, memory, gpu · opens the monitor {key:System monitor}', surface: 'surface.sidebar', line: 'line.border' },
  settings: { title: 'Settings {key:Settings}', note: 'and the theme', surface: 'surface.sidebar', line: 'line.border' },
  header: { title: 'Header · {layout:header}', note: 'title and the screen’s controls', surface: 'surface.window', line: 'line.border' },
  content: { title: 'Content', note: 'fluid · the conversation at most {layout:conversationMax} wide, centred', surface: 'surface.window', line: 'line.border' },
  inspector: { title: 'Inspector · {layout:inspector}', note: 'Sessions only · {key:Show or hide the inspector} · This turn, Context, Model, Helpers, each collapsible', surface: 'surface.inspector', line: 'line.border' },
  composer: { title: 'Composer', note: 'attachments as chips · {key:Commands} commands · {key:Mention a file} files · {key:Send} sends, {key:New line} new line', surface: 'surface.card', line: 'line.strong' },
}

/**
 * The diagram's own geometry, in the window's points, drawn at its scale:
 * the window every screen board draws (1440 × 900, the baseline's), and the
 * sidebar's stack and the composer placed in it as the board placed them.
 * The sidebar, the header and the inspector are the tokens' sizes; these are
 * the one-off spacing around them, never a size the page names.
 */
export const windowDiagram = {
  scale: 0.75,
  window: { width: 1440, height: 900 },
  /** The sidebar's boxes: their inset from its edges, the top they start at, the gap between them, and the room under the last. */
  inset: 12,
  top: 64,
  gap: 16,
  bottom: 20,
  /** The heights of the sidebar's lower three boxes; Sessions takes the rest. */
  places: 64,
  system: 120,
  settings: 64,
  /** The composer: its inset from the content's sides and bottom, and its height. */
  composerInset: 40,
  composerHeight: 140,
} as const
