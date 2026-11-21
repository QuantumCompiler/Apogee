/**
 * A place in the sidebar: one screen of the app, reached by its key. The
 * design's eleven, in its order and names: the Mac app's `Destination`
 * (`lib/src/darwin/source/presentation/shell/ViewModels/Destination.swift`),
 * the old prototype's places bar (`main.html` at `b375fac9`, section
 * `sidebar`). Compositions live under Video, as the design draws them.
 */
export type Destination = 'sessions' | 'models' | 'suites' | 'tasks' | 'graph' | 'video' | 'voice' | 'knowledge' | 'training' | 'library' | 'review'

/** The eleven, in the design's order: ⌘1 to ⌘9, ⌘0, then ⇧⌘R. */
export const destinations: readonly Destination[] = ['sessions', 'models', 'suites', 'tasks', 'graph', 'video', 'voice', 'knowledge', 'training', 'library', 'review']

const titles: Readonly<Record<Destination, string>> = {
  sessions: 'Sessions',
  models: 'Models',
  suites: 'Suites',
  tasks: 'Tasks',
  graph: 'Code graph',
  video: 'Video',
  voice: 'Voice',
  knowledge: 'Knowledge',
  training: 'Training',
  library: 'Library',
  review: 'Review',
}

/** The place's name, as the sidebar and its key's label say it. */
export function titleOf(destination: Destination): string {
  return titles[destination]
}

/**
 * The design item that ports each place not yet drawn here (design 1e): each
 * feature item takes its own line out when it ports its place, and 1s checks
 * none is left.
 */
const portedBy: Readonly<Partial<Record<Destination, string>>> = {
  suites: '1p',
  tasks: '1p',
  graph: '1o',
  voice: '1m',
  knowledge: '1o',
  library: '1n',
  review: '1n',
}

/** What a place not yet ported says: its name, and the item that ports it. */
export interface Placeholder {
  readonly title: string
  readonly item: string
  readonly says: string
}

/** The place's placeholder; `undefined` once its item has ported it. */
export function placeholderOf(destination: Destination): Placeholder | undefined {
  const item = portedBy[destination]
  if (item === undefined) return undefined
  const title = titleOf(destination)
  return { title, item, says: `${title} isn’t drawn here yet. Design item ${item} ports it.` }
}

/** What opens over whatever place is shown, one at a time: the Mac app's `Overlay`. */
export type Overlay = 'monitor' | 'console'
