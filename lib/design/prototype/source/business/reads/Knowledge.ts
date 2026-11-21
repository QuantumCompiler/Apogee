import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/**
 * Knowledge's reads (design 1o): the Mac app's `KnowledgeRecord`,
 * `KnowledgeQuery`, `CollectionRow`, `CollectionInfo` and `RawConversation`
 * (`BusinessReads`, `Knowledge.swift`), each field as the CLI's
 * `knowledge list | query` and `embed list | info` carry it.
 */

const text = (value: JSONValue | undefined): string => JSONValue.string(value) ?? ''
const named = (value: JSONValue | undefined): string | undefined => {
  const said = JSONValue.string(value)
  return said === undefined || said === '' ? undefined : said
}

/** One decision record, as `knowledge list | info --output-format json` carries it: the reasoning, the decision, its branch, and where it came from. */
export interface KnowledgeRecord {
  readonly id: string
  readonly intent: string
  readonly decision: string
  /** `shipped`, `rejected` or `superseded`. */
  readonly status: string
  readonly discipline: string
  /** The artifact the decision produced, as `knowledge link` set it (`downstream_link`). */
  readonly link: string
  readonly source: string
  readonly attribution: string
  readonly timestamp: string
  readonly supersedes: string | undefined
  /** Where the CLI archived the conversation the record was distilled from: whether there is one to show. Never opened here -- the conversation comes from `knowledge info --raw`. */
  readonly rawRef: string | undefined
}

export const KnowledgeRecord = {
  read(value: JSONValue): KnowledgeRecord | undefined {
    const row = JSONValue.object(value)
    const id = named(row?.id)
    if (!row || id === undefined) return undefined
    const provenance = JSONValue.object(row.provenance)
    return {
      id,
      intent: text(row.intent),
      decision: text(row.decision),
      status: text(row.status),
      discipline: text(row.discipline),
      link: text(row.downstream_link),
      source: text(provenance?.source),
      attribution: text(provenance?.attribution),
      timestamp: text(row.timestamp),
      supersedes: named(row.supersedes),
      rawRef: named(row.raw_ref),
    }
  },

  /** Every record of a `knowledge list` document, in its order (newest first). */
  list(document: ReadDocument): KnowledgeRecord[] {
    return (document.entries ?? []).flatMap((entry) => KnowledgeRecord.read(entry) ?? [])
  },

  /** The branches a record can be moved to, as `knowledge status` takes them. */
  statuses: ['shipped', 'rejected', 'superseded'] as const,
}

/** One match of a question: the record and its score on the retriever's own scale. */
export interface KnowledgeMatch {
  readonly record: KnowledgeRecord
  readonly score: number | undefined
}

/** `knowledge query <text> --json`: the records that best answer a question, each with its score, and which retriever ran. */
export interface KnowledgeQuery {
  readonly matches: readonly KnowledgeMatch[]
  /** `lexical`, `vector` or `hybrid`, as the CLI says it ran. */
  readonly retriever: string
  readonly reranked: boolean
}

export const KnowledgeQuery = {
  /** The document's matches; `undefined` for one with no `records`. */
  read(document: ReadDocument): KnowledgeQuery | undefined {
    const records = JSONValue.array(document.value.records)
    if (!records) return undefined
    return {
      matches: records.flatMap((entry) => {
        const match = JSONValue.object(entry)
        const record = match?.record === undefined ? undefined : KnowledgeRecord.read(match.record)
        return record ? [{ record, score: JSONValue.number(match?.score) }] : []
      }),
      retriever: text(document.value.retriever),
      reranked: JSONValue.bool(document.value.reranked) ?? false,
    }
  },

  /** A score as the CLI's retriever scores it, to three places: the Mac's `String(format: "%.3f")`. */
  score(match: KnowledgeMatch): string {
    return match.score === undefined ? '—' : match.score.toFixed(3)
  },
}

/** One collection, as `embed list --output-format json` lists it. */
export interface CollectionRow {
  readonly name: string
  readonly chunks: number
  readonly sources: number
}

export const CollectionRow = {
  list(document: ReadDocument): CollectionRow[] {
    return (document.entries ?? []).flatMap((entry) => {
      const row = JSONValue.object(entry)
      const name = named(row?.name)
      return name === undefined ? [] : [{ name, chunks: JSONValue.int(row?.chunks) ?? 0, sources: JSONValue.int(row?.sources) ?? 0 }]
    })
  },
}

/** A collection's vectors, as `embed info`'s `vectors` object states them: read as the CLI sent them, never counted here. */
export interface CollectionVectors {
  /** How many chunks carry a vector. */
  readonly chunks: number
  /** The vectors' width, `D` in the CLI's `D-d`. */
  readonly dimension: number
  /** How many different widths there are: more than one is the CLI's `MIXED` warning. */
  readonly widths: number
  /** The embedding model the collection is bound to; `undefined` when none is recorded, or when they come from several. */
  readonly model: string | undefined
  /** Each model the vectors come from, when more than one -- an unrecorded one `undefined`. Empty when they come from one. */
  readonly mixed: readonly (string | undefined)[]
}

/** `embed info <collection> --output-format json`: a collection's sources and health, in the CLI's words. */
export interface CollectionInfo {
  readonly name: string
  readonly chunks: number
  /** The text index's health, as `embed info` says it (`ok` …). */
  readonly index: string
  readonly sources: readonly string[]
  /** `undefined` when it is a text index only (`null`). */
  readonly vectors: CollectionVectors | undefined
  readonly path: string
}

/** Models named as the CLI names them (`agentloop::spoken_models`): quoted, `and` before the last, an unrecorded one "an unrecorded model". */
function spoken(models: readonly (string | undefined)[]): string {
  const names = models.map((model) => (model === undefined ? 'an unrecorded model' : `'${model}'`))
  if (names.length <= 1) return names[0] ?? ''
  return `${names.slice(0, -1).join(', ')} and ${names.at(-1) ?? ''}`
}

export const CollectionInfo = {
  read(document: ReadDocument): CollectionInfo | undefined {
    const value = document.value
    const name = JSONValue.string(value.collection)
    if (name === undefined) return undefined
    const vectors = JSONValue.object(value.vectors)
    return {
      name,
      chunks: JSONValue.int(value.chunks) ?? 0,
      index: text(value.index),
      sources: (JSONValue.array(value.sources) ?? []).flatMap((source) => (typeof source === 'string' ? [source] : [])),
      vectors: vectors
        ? {
            chunks: JSONValue.int(vectors.chunks) ?? 0,
            dimension: JSONValue.int(vectors.dimension) ?? 0,
            widths: JSONValue.int(vectors.widths) ?? 1,
            model: named(vectors.model),
            mixed: (JSONValue.array(vectors.mixed) ?? []).map((model) => named(model)),
          }
        : undefined,
      path: text(value.path),
    }
  },

  /**
   * The Vectors line, in the words of the CLI's text-mode `embed info`
   * (`embed.cpp`'s `collection_info_text`), from its document's `vectors`:
   * the Mac's `CollectionsViewModel.vectors` and the old prototype's
   * `knVectors`, word for word.
   */
  vectorsLine(info: CollectionInfo): string {
    const vectors = info.vectors
    if (!vectors) return 'none -- a text index only'
    let line = `${vectors.chunks} of ${info.chunks} chunk${info.chunks === 1 ? '' : 's'}, ${vectors.dimension}-d`
    if (vectors.mixed.length > 0) line += ` [mixed: ${spoken(vectors.mixed)} -- re-ingest every source with one model]`
    else if (vectors.model !== undefined) line += ` [${vectors.model}]`
    else line += ' [model not recorded -- re-ingest to bind]'
    if (vectors.widths > 1) line += ` -- MIXED: ${vectors.widths} widths`
    return line
  },
}

/** A record's archived raw conversation, as `knowledge info <id> --raw` prints it: the conversation, or that the CLI could not read the archive. */
export type RawConversation = { readonly case: 'text'; readonly text: string } | { readonly case: 'unavailable' }

const marker = '-- Raw conversation --'
const unavailableLine = 'Raw conversation: (unavailable)'

export const RawConversation = {
  /** What `knowledge info --raw` printed: what follows the CLI's own marker; `undefined` when neither of its lines is there. */
  printed(said: string): RawConversation | undefined {
    const lines = said.split('\n')
    const start = lines.indexOf(marker)
    if (start >= 0) return { case: 'text', text: lines.slice(start + 1).join('\n').replace(/^\n+|\n+$/g, '') }
    if (lines.includes(unavailableLine)) return { case: 'unavailable' }
    return undefined
  },
}
