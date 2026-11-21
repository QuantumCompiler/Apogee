/**
 * The processes page's data (design 1r): the old BuildProcesses board, "how
 * the app runs Apogee", at 1c's frozen revision `b375fac9`, as data -- its
 * boxes where the board drew them, their words, and what joins them. Its words
 * are the board's, "SwiftUI first" among them, since it describes the Mac app
 * the design is built for; its 74 hex values are the page's tokens now, so it
 * follows the theme. Inline code is written in backticks.
 */

/** A line in a box: a command or a path in the code face, a note in the secondary text, plain words, or an empty line. */
export type ProcessLine = { readonly kind: 'code' | 'note' | 'plain'; readonly text: string } | { readonly kind: 'space' }

/** A box: where it sits on the 1440 × 1040 page, its title, its lines, and whether it is the app itself, drawn in the accent. */
export interface ProcessBox {
  readonly id: string
  readonly x: number
  readonly y: number
  readonly width: number
  readonly height: number
  readonly title: string
  readonly lines: readonly ProcessLine[]
  readonly app?: true
}

/** Two stacked boxes joined by a dashed line, labelled beside it. */
export interface ProcessLink {
  readonly from: string
  readonly to: string
  readonly label: string
}

/** One box fanning out to several, over a bus halfway between them: a line to the bus, the bus, an arrow to each. */
export interface ProcessFan {
  readonly from: string
  readonly to: readonly string[]
  readonly label: string
}

/** The page's size, title and words. */
export const processesPage = {
  width: 1440,
  height: 1040,
  title: 'How the app runs Apogee',
  intro: 'One app process starting `apogee` children and speaking to them over their pipes. It never links Apogee and never opens a port: only `apogee serve` owns one, and that is for server deployments.',
} as const

/** The boxes, as the board drew them: the app between its own files and the media's records, and its six kinds of child on the right. */
export const processBoxes: readonly ProcessBox[] = [
  {
    id: 'files',
    x: 64,
    y: 216,
    width: 300,
    height: 120,
    title: 'The app’s own files',
    lines: [
      { kind: 'code', text: '~/Library/Application Support/Apogee' },
      { kind: 'note', text: 'themes · window layout · open sessions · watched media folders' },
    ],
  },
  {
    id: 'app',
    x: 64,
    y: 400,
    width: 300,
    height: 200,
    title: 'Apogee.app',
    app: true,
    lines: [
      { kind: 'note', text: 'SwiftUI first' },
      { kind: 'space' },
      { kind: 'plain', text: 'one process supervisor' },
      { kind: 'plain', text: 'a protocol client generated from `machine-schema.json`' },
      { kind: 'plain', text: 'stores re-read from the CLI' },
    ],
  },
  {
    id: 'records',
    x: 64,
    y: 664,
    width: 300,
    height: 136,
    title: 'Records beside the media',
    lines: [
      { kind: 'code', text: '~/Movies/Apogee/*.mp4.json' },
      { kind: 'code', text: '~/Music/Apogee/*.wav.json' },
      { kind: 'note', text: 'the media library is these files' },
    ],
  },
  {
    id: 'session',
    x: 700,
    y: 168,
    width: 676,
    height: 92,
    title: 'Each open session: one chat child, alive while it is in view or mid-turn',
    lines: [
      { kind: 'code', text: 'apogee chat --output-format stream-json --input-format stream-json --resume ‹id›' },
      { kind: 'note', text: 'in: user, answer, attach, cancel · out: the turn’s events, a question, then result or error' },
    ],
  },
  {
    id: 'task',
    x: 700,
    y: 308,
    width: 676,
    height: 92,
    title: 'Each task the app starts: one task run child',
    lines: [
      { kind: 'code', text: 'apogee task run "‹goal›" --tools --allow … --rounds 8 --output-format stream-json' },
      { kind: 'note', text: 'task_started, task_plan, task_round, task_finished around each round’s events' },
    ],
  },
  {
    id: 'media',
    x: 700,
    y: 448,
    width: 676,
    height: 92,
    title: 'Each clip, composition or speech: one run',
    lines: [
      { kind: 'code', text: 'apogee video generate | video compose | voice say … --output-format json' },
      { kind: 'note', text: 'its document when it ends · progress is stderr text (G11) · a project file re-read as it saves' },
    ],
  },
  {
    id: 'reads',
    x: 700,
    y: 588,
    width: 676,
    height: 92,
    title: 'Reads: one-shot, run again after every change the app makes',
    lines: [
      { kind: 'code', text: 'models list | info | status · chats list · task list | status · check · graph … --output-format json' },
      { kind: 'note', text: 'no push channel: the app knows what it changed, so it reads that back' },
    ],
  },
  {
    id: 'changes',
    x: 700,
    y: 728,
    width: 676,
    height: 92,
    title: 'Changes: one command each, the one a person would type',
    lines: [
      { kind: 'code', text: 'config set-* · models pull | delete | rename · chats title | delete · auth add · train setup --with …' },
      { kind: 'note', text: 'an API key goes to auth add on stdin, never on a command line' },
    ],
  },
  {
    id: 'machine',
    x: 700,
    y: 868,
    width: 676,
    height: 92,
    title: 'The machine, while the system monitor is open',
    lines: [
      { kind: 'code', text: 'apogee system --output-format json, every 2 s' },
      { kind: 'note', text: 'plus the memory of the app’s own children, which only the app knows' },
    ],
  },
]

/** The app's own files and the media's records, each joined to the app by a dashed line. */
export const processLinks: readonly ProcessLink[] = [
  { from: 'files', to: 'app', label: 'writes' },
  { from: 'app', to: 'records', label: 'reads' },
]

/** The app starting its children. */
export const processFan: ProcessFan = { from: 'app', to: ['session', 'task', 'media', 'reads', 'changes', 'machine'], label: 'starts, over pipes' }
