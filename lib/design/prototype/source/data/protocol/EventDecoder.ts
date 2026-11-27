import { JSONValue } from './JSONValue'
import type { JSONObject } from './JSONValue'
import { ProtocolEvent } from './ProtocolEvent'
import type { Capabilities, ProtocolEventKind, QuestionItem, Source, TodoItem } from './ProtocolEvent'

const { array, bool, int, number, object, string } = JSONValue

/**
 * Reads one stdout line into a `ProtocolEvent`, and never fails: the Mac
 * app's `EventDecoder` (`DataProtocol`, `EventDecoder.swift`). A known type is
 * read into its case; one missing a field its case needs is kept as `unknown`
 * under its own type rather than guessed at. A `task_*` type is kept whole.
 */
export const EventDecoder = {
  decode(line: string): ProtocolEvent {
    const fields = object(JSONValue.parse(line))
    const type = string(fields?.type)
    if (!fields || type === undefined) return new ProtocolEvent({ case: 'unreadable', line }, {})
    const kind: ProtocolEventKind =
      known(type, fields) ?? (type.startsWith('task_') ? { case: 'task', type } : { case: 'unknown', type })
    return new ProtocolEvent(kind, fields)
  },
}

function known(type: string, fields: JSONObject): ProtocolEventKind | undefined {
  const text = (key: string) => string(fields[key])
  switch (type) {
    case 'session': {
      const protocolVersion = int(fields.protocol_version)
      const model = text('model')
      if (protocolVersion === undefined || model === undefined) return undefined
      const chatID = text('chat_id')
      const suite = text('suite')
      return {
        case: 'session',
        session: {
          protocolVersion,
          model,
          nextTurn: int(fields.next_turn) ?? 1,
          capabilities: capabilities(fields.capabilities),
          isPrivate: bool(fields.private) ?? false,
          // M28's: a value that is no id is none, never guessed at; M29's suite the same way.
          chatID: chatID === '' ? undefined : chatID,
          suite: suite === '' ? undefined : suite,
          orchestrating: bool(fields.orchestrating),
        },
      }
    }
    case 'thinking':
      return { case: 'thinking', budgetReached: bool(fields.budget_reached) ?? false }
    case 'thinking_delta': {
      const delta = text('text')
      return delta === undefined ? undefined : { case: 'thinkingDelta', text: delta }
    }
    case 'memory':
      return { case: 'memory', chats: int(fields.chats) ?? 0, decisions: int(fields.decisions) ?? 0 }
    case 'sources': {
      const entries = array(fields.sources)
      return entries === undefined ? undefined : { case: 'sources', sources: entries.flatMap(source) }
    }
    case 'tool_status': {
      const status = text('text')
      return status === undefined ? undefined : { case: 'toolStatus', text: status }
    }
    case 'tool_call': {
      const id = text('id')
      const name = text('name')
      const state = text('state')
      if (id === undefined || name === undefined || state === undefined) return undefined
      return { case: 'toolCall', call: { id, name, state, target: text('target') } }
    }
    case 'notice': {
      const notice = text('text')
      return notice === undefined ? undefined : { case: 'notice', text: notice }
    }
    case 'todo': {
      const items = array(fields.items)
      return items === undefined ? undefined : { case: 'todo', items: items.flatMap(todoItem) }
    }
    case 'answer_start':
      return { case: 'answerStart' }
    case 'answer_delta': {
      const delta = text('text')
      return delta === undefined ? undefined : { case: 'answerDelta', text: delta }
    }
    case 'answer_end':
      return { case: 'answerEnd' }
    case 'result': {
      const answer = text('text')
      if (answer === undefined) return undefined
      const usage = object(fields.usage)
      return {
        case: 'result',
        result: {
          text: answer,
          model: text('model'),
          finishReason: text('finish_reason'),
          usage: fields.usage === undefined ? undefined : { inputTokens: int(usage?.input_tokens), outputTokens: int(usage?.output_tokens) },
        },
      }
    }
    case 'question': {
      const questions = array(fields.questions)
      if (questions === undefined) return undefined
      return {
        case: 'question',
        question: {
          kind: text('kind'),
          tool: text('tool'),
          target: text('target'),
          outbound: bool(fields.outbound) ?? false,
          detail: text('detail'),
          items: questions.map(questionItem),
        },
      }
    }
    case 'error': {
      const message = text('message')
      return message === undefined ? undefined : { case: 'error', message }
    }
    default:
      return undefined
  }
}

function capabilities(value: JSONValue | undefined): Capabilities | undefined {
  const fields = object(value)
  if (!fields) return undefined
  const strings = (key: string) => (array(fields[key]) ?? []).flatMap((entry) => (typeof entry === 'string' ? [entry] : []))
  return {
    events: strings('events'),
    accepts: strings('accepts'),
    tools: bool(fields.tools) ?? false,
    ask: bool(fields.ask) ?? false,
    schema: string(fields.schema) ?? '',
  }
}

function source(value: JSONValue): Source[] {
  const entry = object(value)
  const file = string(entry?.file)
  if (!entry || file === undefined) return []
  const ints = (key: string) => array(entry[key])?.flatMap((part) => (int(part) === undefined ? [] : [part as number]))
  const numbers = (key: string) => array(entry[key])?.flatMap((part) => (typeof part === 'number' ? [part] : []))
  return [
    { file, pages: ints('pages'), lines: ints('lines'), times: numbers('times'), score: number(entry.score), retriever: string(entry.retriever) },
  ]
}

function todoItem(value: JSONValue): TodoItem[] {
  const item = object(value)
  const text = string(item?.text)
  return text === undefined ? [] : [{ text, state: string(item?.state) ?? 'pending' }]
}

function questionItem(value: JSONValue): QuestionItem {
  const item = object(value)
  return {
    header: string(item?.header) ?? '',
    question: string(item?.question) ?? '',
    multiSelect: bool(item?.multi_select) ?? false,
    options: (array(item?.options) ?? []).flatMap((option) => {
      const label = string(object(option)?.label)
      return label === undefined ? [] : [{ label, description: string(object(option)?.description) ?? '' }]
    }),
  }
}
