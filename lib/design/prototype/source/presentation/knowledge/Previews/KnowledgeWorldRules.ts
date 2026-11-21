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
