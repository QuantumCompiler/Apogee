import type { Rule, Rules } from '../../../infrastructure/process/StandInChild'

/**
 * The Knowledge place's stand-in answers (design 1o), every one a recording
 * of the Mac app's or a `*-handmade*` file in the CLI's shape: `knowledge
 * list` (`knowledge-list.json`), the old question asked (`knowledge-query.json`),
 * the rejected record's raw conversation (`knowledge-info-raw-1e2354-handmade.txt`,
 * `knowledge-info-raw.txt`'s shape), `embed list` (the merged
 * `collections-list-handmade.json`), each collection's `embed info`
 * (`collection-info.json` for notes, `collection-info-vectors-mixed.json` for
 * mixed, `collection-info-knowledge-handmade.json` for the knowledge store's
 * own) and notes searched (`collection-query.txt`).
 *
 * Each file is named by its file name, looked up in `KnowledgeWorlds.ts`'
 * `recordings`, where the `?raw` imports are: this module imports none, so
 * the fixture check lists what every preset names without loading a fixture.
 */

/** Where a world's recordings come from: `KnowledgeWorlds.ts`' `recording`, or a test's lookup that lists the names. */
export type Lookup = (name: string) => string

/** The question the old Records tab asks (`kno.question`): what `knowledge-query.json` answers. */
export const recordsQuestion = 'why was the cancel button removed'

/** The search the old Collections tab makes of notes (`kno.cq`): what `collection-query.txt` printed. */
export const collectionQuestion = 'how does a heat pump work'

/** The record the old Records tab opens, the rejected one, whose raw conversation it shows. */
export const rejectedRecord = 'kr-20261021T045944Z-1e2354'

/** The collections whose search the CLI recorded: a search of another says the old toast (`kno.search`). */
export const recordedSearches: readonly string[] = ['notes']

/** The Knowledge presets, in the old `data-props` order: what the fixture check walks. Each reads the place's answers, every preset's. */
export const knowledgePresets = ['knowledge', 'knowledge:collections', 'knowledge:mixed'] as const

/** The presets drawn for GUI M22, a run that left a note: each reads the place's answers and the noted sandbox's (`knowledgeNoteRules`). */
export const knowledgeNotedPresets = ['knowledge:ingested-with-a-note', 'knowledge:captured-with-a-note'] as const

/** The recorded ingest the Collections preset runs (GUI M22): into `my notes`, a name the config can't hold, from the sandbox's one note. */
export const notedIngest = { collection: 'my notes', path: '/Users/someone/docs/a note.txt' } as const

/** The conversation the recorded capture was given (GUI M22), in a suite whose verifier validates the extraction. */
export const notedCapture = 'Ada: testers kept mistaking the cancel button for back, so we removed it. Shipped in PROJ-42.'

/** `embed ingest -- <collection> <path>`: the Mac's line (GUI M21), the collection and the path after `--`. */
export const ingestArguments = (collection: string, path: string): string[] => ['embed', 'ingest', '--', collection, path]

/** `knowledge capture -- <text>`: the Mac's line, the text after `--`. */
export const captureArguments = (text: string): string[] => ['knowledge', 'capture', '--', text]

/**
 * What the two noted presets add (GUI M22): the built CLI's runs in its
 * sandboxes (the Mac's `tests/business/jobs/Fixtures`), each its stdout and
 * stderr -- the ingest into `my notes` that could not register it, and the
 * capture its suite's verifier validated -- and what those sandboxes read
 * back after them: the store listed and `my notes`' page, and the records.
 */
export function knowledgeNoteRules(look: Lookup): Required<Pick<Rules, 'reads' | 'jobs'>> {
  return {
    reads: [
      read(['embed', 'list'], look('embed-list-my-notes.json')),
      read(['embed', 'info', notedIngest.collection], look('embed-info-my-notes.json')),
      read(['knowledge', 'list'], look('knowledge-list-validated.json')),
    ],
    jobs: [
      { arguments: ingestArguments(notedIngest.collection, notedIngest.path), exact: true, answer: { stdout: look('embed-ingest-unregistrable.out'), stderr: look('embed-ingest-unregistrable.err') } },
      { arguments: captureArguments(notedCapture), exact: true, answer: { stdout: look('knowledge-capture-validated.out'), stderr: look('knowledge-capture-validated.err') } },
    ],
  }
}

/** The files the noted presets add, each once: what the fixture check holds to exist on them. */
export function knowledgeNoteNames(): string[] {
  const named = new Set<string>()
  knowledgeNoteRules((name) => (named.add(name), ''))
  return [...named]
}

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })
const exact = (args: readonly string[], stdout: string): Rule => ({ arguments: args, exact: true, answer: { stdout } })

/** The `knowledge query` the Records tab runs for `question` over every branch: the Mac's `--status "" --json -- <question>`. */
export const queryArguments = (question: string, branch = ''): string[] => ['knowledge', 'query', '--status', branch, '--json', '--', question]

/** What the Knowledge place reads and runs on every preset: each read by its arguments, each command for its exact ones. */
export function knowledgeRules(look: Lookup): Required<Pick<Rules, 'reads' | 'commands'>> {
  return {
    reads: [
      read(['knowledge', 'list'], look('knowledge-list.json')),
      read(['embed', 'list'], look('collections-list-handmade.json')),
      read(['embed', 'info', 'knowledge'], look('collection-info-knowledge-handmade.json')),
      read(['embed', 'info', 'mixed'], look('collection-info-vectors-mixed.json')),
      read(['embed', 'info', 'notes'], look('collection-info.json')),
    ],
    commands: [
      exact(queryArguments(recordsQuestion), look('knowledge-query.json')),
      exact(['knowledge', 'info', rejectedRecord, '--raw'], look('knowledge-info-raw-1e2354-handmade.txt')),
      exact(['embed', 'query', 'notes', '--', collectionQuestion], look('collection-query.txt')),
    ],
  }
}

/** The file names the place's answers look up, each once: what the fixture check holds to exist on every Knowledge preset. */
export function knowledgePlaceNames(): string[] {
  const named = new Set<string>()
  knowledgeRules((name) => (named.add(name), ''))
  return [...named]
}
