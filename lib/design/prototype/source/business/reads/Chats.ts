import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'
import type { Field } from './Models'

const { int, number, object, string } = JSONValue

/** A text the CLI writes, `undefined` where it wrote none or an empty one. */
function nonEmpty(value: JSONValue | undefined): string | undefined {
  const text = string(value)
  return text === undefined || text === '' ? undefined : text
}

/** `2026-10-21T00:38:43Z` as written, when it parses as a time; `undefined` otherwise. */
function stamp(value: JSONValue | undefined): string | undefined {
  const text = string(value)
  return text !== undefined && !Number.isNaN(Date.parse(text)) ? text : undefined
}

/**
 * One saved conversation, as `chats list --output-format json` lists it: the
 * Mac app's `ChatSummary` (`BusinessReads`, `Chats.swift`). `updated` is kept
 * as the CLI wrote it.
 */
export interface ChatSummary {
  /** What `--resume` and every `chats` verb take. */
  readonly id: string
  readonly name: string
  /** When it last changed; `undefined` when the CLI's stamp did not parse. */
  readonly updated: string | undefined
  readonly turns: number
  /** The backend it runs on, since the CLI's 52a; `undefined` from a build before it. */
  readonly backend: string | undefined
  /** The roster model it pinned there. */
  readonly model: string | undefined
}

export const ChatSummary = {
  /** The rows of a `chats list` document, newest first as listed; a row without an id is skipped, never guessed. */
  list(document: ReadDocument): ChatSummary[] {
    return (document.entries ?? []).flatMap((entry) => {
      const row = object(entry)
      const id = nonEmpty(row?.id)
      if (id === undefined) return []
      return [{ id, name: nonEmpty(row?.name) ?? id, updated: stamp(row?.updated), turns: int(row?.turns) ?? 0, backend: nonEmpty(row?.backend), model: nonEmpty(row?.model) }]
    })
  },
}

/**
 * One saved conversation's details, as `chats info <id> --output-format json`
 * types them since the CLI's M31 (`object: "chat.info"`): the Mac app's
 * `ChatDetails` (`Chats.swift`).
 */
export interface ChatDetails {
  readonly id: string
  readonly name: string
  readonly customName: string
  readonly title: string
  /** Empty for the default -- the text's `(default)`. */
  readonly backend: string
  readonly model: string
  readonly started: string
  readonly updated: string
  readonly turns: number
  readonly messages: number
  readonly compactions: number
  readonly temperature: number | undefined
  readonly maxTokens: number | undefined
  readonly schemaVersion: number | undefined
  /** The facts as the details sheet lists them, under the text's own labels, so the two readings of one chat agree line for line. */
  readonly fields: readonly Field[]
}

export const ChatDetails = {
  /** The `chat.info` document's facts; `undefined` for any other document. */
  of(document: ReadDocument): ChatDetails | undefined {
    const v = document.value
    const id = nonEmpty(v.id)
    if (document.object !== 'chat.info' || id === undefined) return undefined
    const details = {
      id,
      name: nonEmpty(v.name) ?? id,
      customName: string(v.custom_name) ?? '',
      title: string(v.title) ?? '',
      backend: string(v.backend) ?? '',
      model: string(v.model) ?? '',
      started: string(v.started) ?? '',
      updated: string(v.updated) ?? '',
      turns: int(v.turns) ?? 0,
      messages: int(v.messages) ?? 0,
      compactions: int(v.compactions) ?? 0,
      temperature: number(v.temperature),
      maxTokens: int(v.max_tokens),
      schemaVersion: int(v.schema_version),
    }
    const fields: Field[] = [{ label: 'id', value: id }]
    if (details.customName !== '') fields.push({ label: 'name', value: details.customName })
    if (details.title !== '') fields.push({ label: 'title', value: details.title })
    fields.push({ label: 'backend', value: details.backend === '' ? '(default)' : details.backend })
    if (details.model !== '') fields.push({ label: 'model', value: details.model })
    fields.push(
      { label: 'started', value: details.started },
      { label: 'updated', value: details.updated },
      { label: 'turns', value: String(details.turns) },
      { label: 'messages', value: String(details.messages) },
      { label: 'compacted', value: details.compactions > 0 ? `${details.compactions} time(s)` : 'never' },
    )
    if (details.temperature !== undefined) fields.push({ label: 'temp', value: String(details.temperature) })
    if (details.maxTokens !== undefined) fields.push({ label: 'max_tok', value: String(details.maxTokens) })
    if (details.schemaVersion === 0) fields.push({ label: 'schema', value: 'legacy (pre-versioning)' })
    return { ...details, fields }
  },
}

/** One chat's summary, as the index recorded it. */
export interface RecallSummary {
  /** The chat it summarises: what `chats show` and `--resume` take. */
  readonly chat: string
  /** The name it was recorded under; an older entry may leave it empty. */
  readonly name: string
  readonly updated: string | undefined
  /** The text the model is handed, whole. */
  readonly summary: string
  /** What a person knows it by: its name, else its chat's id. */
  readonly title: string
}

/**
 * What recall holds of earlier chats, as `chats recall --output-format json`
 * lists it since the CLI's M31: the Mac app's `RecallListing` (`Chats.swift`).
 */
export interface RecallListing {
  readonly summaries: readonly RecallSummary[]
  /** The index's path, as the CLI names it. */
  readonly index: string
  /** How many chats are due a summary and not yet summarised. */
  readonly pending: number
  /** `recall holds 3 chat summaries in <index>`, and how many more are due one. */
  readonly countLine: string
  /** Whether Forget has anything to erase: the CLI says "nothing to forget" otherwise. */
  readonly holdsAny: boolean
}

export const RecallListing = {
  /** The listing's facts; `undefined` for any other document. */
  of(document: ReadDocument): RecallListing | undefined {
    const index = string(document.value.index)
    const data = document.entries
    if (document.object !== 'list' || data === undefined || index === undefined) return undefined
    const summaries = data.flatMap((entry): RecallSummary[] => {
      const row = object(entry)
      const chat = nonEmpty(row?.chat)
      if (chat === undefined) return []
      const name = string(row?.name) ?? ''
      return [{ chat, name, updated: stamp(row?.updated), summary: string(row?.summary) ?? '', title: name === '' ? chat : name }]
    })
    const pending = int(document.value.pending) ?? 0
    const count = summaries.length
    const countLine = `recall holds ${count} chat summar${count === 1 ? 'y' : 'ies'} in ${index}${pending === 0 ? '' : `, ${pending} more due one`}`
    return { summaries, index, pending, countLine, holdsAny: count > 0 || pending > 0 }
  },
}
