import collectionInfo from '@recorded/business/reads/Fixtures/collection-info.json?raw'
import collectionInfoMixed from '@recorded/business/reads/Fixtures/collection-info-vectors-mixed.json?raw'
import collectionQuery from '@recorded/business/reads/Fixtures/collection-query.txt?raw'
import knowledgeList from '@recorded/business/reads/Fixtures/knowledge-list.json?raw'
import knowledgeQuery from '@recorded/business/reads/Fixtures/knowledge-query.json?raw'
import knowledgeInfoRawOther from '@recorded/business/reads/Fixtures/knowledge-info-raw.txt?raw'
import knowledgeListOne from '@recorded/business/reads/Fixtures/knowledge-list-one.json?raw'
import collectionInfoKnowledge from '../../../../fixtures/business/reads/collection-info-knowledge-handmade.json?raw'
import collectionsList from '../../../../fixtures/business/reads/collections-list-handmade.json?raw'
import knowledgeInfoRaw from '../../../../fixtures/business/reads/knowledge-info-raw-1e2354-handmade.txt?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import { knowledgeRules } from './KnowledgeWorldRules'
import type { Lookup } from './KnowledgeWorldRules'

export * from './KnowledgeWorldRules'

/**
 * The Knowledge place's recordings (design 1o), each a `?raw` import the
 * build inlines, named in `recordings` by its file name; the answers built
 * from them are `KnowledgeWorldRules.ts`, which imports none. Three are
 * hand-made in the CLI's shape under the design's `fixtures/`:
 * - `knowledge-info-raw-1e2354-handmade.txt`: `knowledge-info-raw.txt`
 *   byte for byte but for the rejected record's id, branch and capture time
 *   from `knowledge-list.json` -- the old prototype's three lines, which are
 *   that recording's conversation;
 * - `collections-list-handmade.json`: `collections-list.json`'s knowledge and
 *   notes rows and `collections-list-vectors.json`'s mixed row, in one list,
 *   so every preset lists the old three;
 * - `collection-info-knowledge-handmade.json`: `collection-info.json`'s shape
 *   for the knowledge store's own collection, its three records its sources.
 *
 * `knowledge-list-one.json` and `knowledge-info-raw.txt` hold the one raw
 * conversation the CLI recorded, of another record (`kr-20261026T142411Z-2e8272`):
 * they back a preview of their own (`recordedRaw`).
 */
const recordings: Readonly<Record<string, string>> = {
  'knowledge-list.json': knowledgeList,
  'knowledge-query.json': knowledgeQuery,
  'knowledge-list-one.json': knowledgeListOne,
  'knowledge-info-raw.txt': knowledgeInfoRawOther,
  'collection-info.json': collectionInfo,
  'collection-info-vectors-mixed.json': collectionInfoMixed,
  'collection-query.txt': collectionQuery,
  'knowledge-info-raw-1e2354-handmade.txt': knowledgeInfoRaw,
  'collections-list-handmade.json': collectionsList,
  'collection-info-knowledge-handmade.json': collectionInfoKnowledge,
}

/** A file's text, by its name; a name no import holds is refused, naming it. */
export function recording(name: string): string {
  const text = recordings[name]
  if (text === undefined) throw new Error(`no fixture named ${name}`)
  return text
}

/** What Knowledge reads and runs on every preset (`Presets.ts`' `modelling`). */
export function knowledgePlaceRules(look: Lookup = recording): Required<Pick<Rules, 'reads' | 'commands'>> {
  return knowledgeRules(look)
}

/** The one raw conversation the CLI recorded, of another record: its list and its `knowledge info --raw`, for the preview of a recorded conversation. */
export function recordedRawRules(look: Lookup = recording): Required<Pick<Rules, 'reads' | 'commands'>> {
  return {
    reads: [{ arguments: ['knowledge', 'list'], answer: { stdout: look('knowledge-list-one.json') } }],
    commands: [{ arguments: ['knowledge', 'info', 'kr-20261026T142411Z-2e8272', '--raw'], exact: true, answer: { stdout: look('knowledge-info-raw.txt') } }],
  }
}
