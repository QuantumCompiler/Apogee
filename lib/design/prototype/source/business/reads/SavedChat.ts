import { JSONValue } from '../../data/protocol/JSONValue'
import type { JSONObject } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/** One thing `chats show`'s loader could not read (the CLI's 55b): its kind, `field_dropped`, and the CLI's sentence for it. */
export interface SavedWarning {
  readonly kind: string
  readonly message: string
}

/** An attachment, named, with how many files and their size where the document says. */
export interface SavedAttachment {
  readonly name: string
  readonly files: number | undefined
  readonly bytes: number | undefined
}

/** A tool a turn called: its arguments in one line, and the size of what it returned. */
export interface SavedToolCall {
  readonly name: string
  readonly summary: string
  readonly resultBytes: number | undefined
}

/** One entry of a saved conversation: a turn, or a compaction where it happened. */
export interface SavedTurn {
  /** The conversation's turn count once the entry ended; `undefined` when the document leaves it out. */
  readonly turn: number | undefined
  /** The person's words; empty on a compaction. */
  readonly text: string
  readonly attachments: readonly SavedAttachment[]
  /** The model's words in the turn, every message's joined. */
  readonly answer: string
  readonly tools: readonly SavedToolCall[]
  /** Whether the history holds the model's answer: `false` for a turn cancelled or failed. */
  readonly answered: boolean
  /** On a compaction alone: the summary that stands in for the turns before it. */
  readonly compaction: string | undefined
}

/**
 * A saved conversation's turns, as `chats show <id> --output-format json`
 * reads them (the CLI's M27): what a session that resumes it draws above its
 * live turns. The Mac app's `SavedChat` (`BusinessReads`, `SavedChat.swift`),
 * read as tolerantly: a field an older or newer binary leaves out reads as
 * empty, never a failed read (design 1v).
 */
export interface SavedChat {
  readonly id: string
  /** As `chats list`'s row names it; the id when it has no name. */
  readonly name: string
  readonly attachments: readonly SavedAttachment[]
  readonly turns: readonly SavedTurn[]
  /** What the loader could not read (55b); `undefined` from a binary before it, whose document has none. */
  readonly warnings: readonly SavedWarning[] | undefined
}

const { array, bool, int, object, string } = JSONValue

function attachments(value: JSONValue | undefined): SavedAttachment[] {
  return (array(value) ?? []).flatMap((entry) => {
    const fields = object(entry)
    const name = string(fields?.name)
    return name === undefined || name === '' ? [] : [{ name, files: int(fields?.files), bytes: int(fields?.bytes) }]
  })
}

function turn(entry: JSONValue): SavedTurn[] {
  const fields: JSONObject | undefined = object(entry)
  if (!fields) return []
  const user = object(fields.user)
  const answer = object(fields.answer)
  const text = string(answer?.text) ?? ''
  const tools = (array(answer?.tools) ?? []).flatMap((call) => {
    const tool = object(call)
    const name = string(tool?.name)
    return name === undefined || name === '' ? [] : [{ name, summary: string(tool?.summary) ?? '', resultBytes: int(tool?.result_bytes) }]
  })
  const compacted = object(fields.compacted)
  return [
    {
      turn: int(fields.turn),
      text: string(user?.text) ?? '',
      attachments: attachments(user?.attachments),
      answer: text,
      tools,
      answered: bool(fields.answered) ?? !(text === '' && tools.length === 0),
      compaction: compacted ? (string(compacted.summary) ?? '') : undefined,
    },
  ]
}

export const SavedChat = {
  /** The chat a `chats show` document reads as; `undefined` for another read's document, or one with no `turns`. */
  of(document: ReadDocument): SavedChat | undefined {
    const object = document.object
    if (object !== undefined && object !== 'chat') return undefined
    const turns = array(document.value.turns)
    if (!turns) return undefined
    const id = string(document.value.id) ?? ''
    const name = string(document.value.name)
    const warnings = array(document.value.warnings)
    return {
      id,
      name: name === undefined || name === '' ? id : name,
      attachments: attachments(document.value.attachments),
      turns: turns.flatMap(turn),
      warnings: warnings?.flatMap((entry) => {
        const fields = JSONValue.object(entry)
        const message = string(fields?.message)
        return message === undefined || message === '' ? [] : [{ kind: string(fields?.kind) ?? '', message }]
      }),
    }
  },

  /** The read's arguments after `apogee`, for the chat `id`. */
  arguments(id: string): string[] {
    return ['chats', 'show', id]
  },
}
