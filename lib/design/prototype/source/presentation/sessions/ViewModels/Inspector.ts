import type { ChatSummary, RecallListing } from '../../../business/reads/Chats'
import type { Field, ModelKind, ModelRow } from '../../../business/reads/Models'
import type { ModelFace, SessionViewModel } from '../../session/ViewModels/SessionViewModel'
import { ChatGroup } from './ChatGroup'
import type { InspectorTurn } from './InspectorTurn'

/** The inspector's six sections, each folding: the old prototype's `insOpen` keys. */
export type InspectorSection = 'session' | 'turn' | 'context' | 'model' | 'helpers' | 'recall'

export const inspectorSections: readonly InspectorSection[] = ['session', 'turn', 'context', 'model', 'helpers', 'recall']

/** The tone a status or a value is drawn in: the old prototype's `--status-accent`, `--status-warning`, `--status-error`, or the quiet tertiary text. */
export type Tone = 'accent' | 'warning' | 'error' | 'tertiary' | 'primary'

/** One THIS TURN row: the old prototype's `row(label, value, sub, time, opt)`. */
export interface TurnRow {
  readonly label: string
  readonly value: string
  readonly sub: string
  readonly time: string
  /** A tool's row: its label and value in the mono face. */
  readonly tool: boolean
  readonly tone: Tone
}

/** Everything the inspector draws, as words (absence said explicitly, so the per-preset walk reads it). */
export interface InspectorFace {
  /** `· turn at 14:03`, `· latest turn`, `· no turn yet`. */
  readonly turnLabel: string
  /** `Expand all`, or `Collapse all` once every section is open. */
  readonly allLabel: string
  readonly sections: Readonly<Record<InspectorSection, boolean>>
  readonly session: { readonly lines: ReadonlyArray<{ readonly label: string; readonly value: string; readonly mono: boolean }>; readonly chatActs: boolean; readonly canCompact: boolean }
  readonly turn: { readonly status: { readonly words: string; readonly tone: Tone }; readonly rows: readonly TurnRow[] }
  readonly context: {
    readonly summary: string
    readonly known: boolean
    readonly percent: number
    readonly warns: boolean
    /** `8,192-token window`, or `window not reported`. */
    readonly window: string
    /** `27,612`; empty where unknown. */
    readonly used: string
    /** Why it cannot be known; empty where it is. */
    readonly unknownWords: string
    readonly lines: ReadonlyArray<{ readonly label: string; readonly value: string }>
    readonly canCompact: boolean
  }
  readonly model: { readonly summary: ModelFace; readonly facts: ReadonlyArray<{ readonly label: string; readonly value: string }> }
  readonly helpers: { readonly summary: string; readonly rows: ReadonlyArray<{ readonly label: string; readonly value: string; readonly set: boolean }>; readonly failure: string }
  readonly recall: {
    /** The count line `chats recall` says; empty unread. */
    readonly line: string
    readonly summaries: ReadonlyArray<{ readonly chat: string; readonly title: string; readonly updated: string; readonly chosen: boolean; readonly text: string }>
    readonly failure: string
    readonly canForget: boolean
  }
}

/** What the inspector is built from: the open session, its saved row, the latest turn's events and the reads. */
export interface InspectorInput {
  readonly session: SessionViewModel
  readonly chat: ChatSummary | undefined
  readonly turn: InspectorTurn | undefined
  readonly now: Date
  readonly sections: Readonly<Record<InspectorSection, boolean>>
  readonly compacting: boolean
  /** `models info <model>`'s fields, for the model the session runs on; empty unread. */
  readonly modelInfo: readonly Field[]
  readonly modelRow: ModelRow | undefined
  readonly roles: readonly Field[]
  readonly rolesFailure: string | undefined
  readonly recall: RecallListing | undefined
  readonly recallFailure: string | undefined
  readonly recallSelection: string | undefined
}

const helperOrder: ReadonlyArray<readonly [string, string]> = [
  ['embedding', 'Embedding'],
  ['vision', 'Vision'],
  ['transcription', 'Transcription'],
  ['utility', 'Utility'],
  ['extraction', 'Extraction'],
]

const plural = (count: number, word: string) => `${count} ${word}${count === 1 ? '' : 's'}`
const capitalized = (text: string) => text.charAt(0).toUpperCase() + text.slice(1)

export const Inspector = {
  face(input: InspectorInput): InspectorFace {
    const { session, chat, sections } = input
    const face = session.face()
    const allOpen = Object.values(sections).every((open) => open)
    const meter = session.meter
    const model = session.model
    const value = (label: string) => input.modelInfo.find((field) => field.label === label)?.value
    const saved = chat !== undefined
    const privately = session.privateMode === 'nothingWritten'
    const recalled = face.rows.filter((row) => row.kind === 'recalled').reduce((count, row) => count + (row.kind === 'recalled' ? row.chats.length : 0), 0)
    const roles = helperOrder.map(([key, label]) => {
      const role = input.roles.find((field) => field.label === key)?.value ?? ''
      return { label, value: role === '' ? 'not read' : role, set: role !== '' && !role.startsWith('(unset') }
    })
    const set = roles.filter((row) => row.set).length
    return {
      turnLabel: chat ? `· turn at ${ChatGroup.when(chat, input.now)}` : input.turn || session.hasTurns ? '· latest turn' : '· no turn yet',
      allLabel: allOpen ? 'Collapse all' : 'Expand all',
      sections,
      session: {
        lines: privately
          ? [{ label: 'Name', value: session.title, mono: false }]
          : !saved
            ? [
                { label: 'Name', value: session.title, mono: false },
                { label: 'Saved', value: 'after its first turn', mono: false },
              ]
            : [
                { label: 'Name', value: chat.name, mono: false },
                { label: 'Turns', value: String(chat.turns), mono: false },
                { label: 'Updated', value: chat.updated ?? 'not recorded', mono: true },
                { label: 'Id', value: chat.id, mono: true },
              ],
        chatActs: saved && !privately,
        canCompact: saved && !input.compacting,
      },
      turn: Inspector.turn(input),
      context: {
        summary: meter.percent === undefined ? 'unknown' : `${meter.percent}%`,
        known: meter.percent !== undefined,
        percent: meter.percent ?? 0,
        warns: meter.warns,
        window: session.contextWindow === undefined ? 'window not reported' : `${session.contextWindow.toLocaleString('en-US')}-token window`,
        used: meter.used === undefined ? '' : meter.used.toLocaleString('en-US'),
        unknownWords:
          meter.percent !== undefined
            ? ''
            : model.kind === 'subscription'
              ? `Not reported. The ${capitalized(model.name)} app doesn’t share token counts, so Apogee can’t tell how full the window is.`
              : 'Not reported. No answer has reported its token counts yet, so Apogee can’t tell how full the window is.',
        lines: [
          ...(session.attachments.length > 0 ? [{ label: 'Attached', value: session.attachments.join(', ') }] : []),
          ...(recalled > 0 ? [{ label: 'Recalled', value: plural(recalled, 'past session') }] : []),
        ],
        // As the old `cantCompact`: off while the context is unknown (a defined percent means both the tokens and the window are), under 4,000 tokens, or once the session ended.
        canCompact: meter.percent !== undefined && (meter.used ?? 0) >= 4000 && session.transcript.status.case !== 'ended',
      },
      model: { summary: model, facts: Inspector.facts(model.kind, value, input.modelRow) },
      helpers: { summary: `${set} set · ${roles.length - set} fall back`, rows: roles, failure: input.rolesFailure ?? '' },
      recall: {
        line: input.recall?.countLine ?? '',
        summaries: (input.recall?.summaries ?? []).map((summary) => ({
          chat: summary.chat,
          title: summary.title,
          updated: summary.updated ?? 'undated',
          chosen: summary.chat === input.recallSelection,
          text: summary.chat === input.recallSelection ? summary.summary : '',
        })),
        failure: input.recallFailure ?? '',
        canForget: input.recall?.holdsAny ?? input.recallFailure !== undefined,
      },
    }
  },

  /** The model's facts, from `models info`, under the old prototype's labels where the CLI states the fact. */
  facts(kind: ModelKind | 'unknown', value: (label: string) => string | undefined, row: ModelRow | undefined): ReadonlyArray<{ readonly label: string; readonly value: string }> {
    const type = value('type') ?? row?.backendType ?? ''
    const window = value('window') ?? 'not reported'
    switch (kind) {
      case 'local':
        return [
          { label: 'Format', value: row?.group ?? 'not reported' },
          { label: 'Runs on', value: type === '' ? 'this Mac' : `${type} on this Mac` },
          { label: 'Size', value: value('size') ?? 'not reported' },
          { label: 'Window', value: window },
        ]
      case 'api':
        return [
          { label: 'Runs on', value: `the ${type} API` },
          { label: 'Billing', value: 'per use' },
          { label: 'Key', value: value('provider') ?? 'not reported' },
          { label: 'Window', value: window },
        ]
      case 'subscription':
        return [
          { label: 'Runs on', value: type },
          { label: 'Billing', value: `your ${type} subscription` },
          { label: 'Usage', value: 'not reported' },
          { label: 'Window', value: window },
        ]
      default:
        return [
          { label: 'Runs on', value: type === '' ? 'not reported' : type },
          { label: 'Window', value: window },
        ]
    }
  },

  /** THIS TURN: its status, and its rows from the replayed events, in the old prototype's order. */
  turn(input: InspectorInput): InspectorFace['turn'] {
    const { session, turn } = input
    const status = session.transcript.status.case
    if (!turn) {
      return {
        status: status === 'ended' ? { words: 'ended', tone: 'error' } : { words: 'idle', tone: 'tertiary' },
        rows: [{ label: 'Turn', value: 'nothing sent yet', sub: '', time: '', tool: false, tone: 'tertiary' }],
      }
    }
    const row = (label: string, value: string, sub = '', options: { tool?: boolean; tone?: Tone } = {}): TurnRow => ({ label, value, sub, time: '', tool: options.tool ?? false, tone: options.tone ?? 'primary' })
    const rows: TurnRow[] = []
    if (turn.thought) rows.push(row('Thinking', session.thinkLabel))
    for (const line of turn.helpers) {
      // A play's stage (`stage 1/2 summarize — asking utility (helper): …`) by its count; a helper's line (`utility — rewriting …`) by its role.
      const stage = /^stage (\d+\/\d+) (.*)$/.exec(line)
      const split = line.indexOf(' — ')
      rows.push(stage ? row(`Stage ${stage[1] ?? ''}`, stage[2] ?? '') : split < 0 ? row('Status', line) : row(capitalized(line.slice(0, split)), line.slice(split + 3)))
    }
    if (turn.recalled.length > 0) rows.push(row('Memory', `Recalled ${plural(turn.recalled.length, 'past session')}`, turn.recalled.map((chat) => chat.name).join(', ')))
    if (turn.sources.length > 0) rows.push(row('Search', [...new Set(turn.sources.map((source) => source.file))].join(', '), plural(turn.sources.length, 'excerpt')))
    const cards = session.face().rows
    const question = turn.question
    for (const call of turn.calls) {
      if (question?.kind === 'permission' && call.name === question.tool) continue
      rows.push(row(call.name, call.target ?? '', call.state, { tool: true }))
    }
    if (question?.kind === 'permission') {
      const card = cards.findLast((entry) => entry.kind === 'permission')
      // The old row's words for a call at the gate (`rtStatus`, `main.html:4979` at `b375fac9`).
      const state = card?.kind !== 'permission' ? 'needs your OK' : card.answered !== '' ? card.answered : card.closed ? 'not answered: the turn ended first' : 'needs your OK'
      const counts = card?.kind === 'permission' ? `+${card.added} −${card.removed} · ` : ''
      rows.push(row(question.tool ?? 'permission', question.target ?? '', `${counts}${state}`, { tool: true }))
    } else if (question) {
      const card = cards.findLast((entry) => entry.kind === 'question')
      const answered = card?.kind === 'question' ? card.answered : []
      const state = answered.length > 0 ? `→ ${answered.join(', ')}` : card?.kind === 'question' && card.closed ? 'not answered: the turn ended first' : 'waiting for you'
      rows.push(row('Question', question.items[0]?.header ?? question.items[0]?.question ?? '', state))
    }
    if (turn.error !== undefined) {
      // As the old failed frame: the model that failed to load, in the error color, the CLI's words under it; no usage row, since the turn ended without one.
      const card = cards.findLast((entry) => entry.kind === 'error')
      const loadFailed = card?.kind === 'error' && card.loadFailed
      const model = session.transcript.items.findLast((item) => item.content.case === 'error')
      const failed = model?.content.case === 'error' ? (model.content.model ?? session.model.name) : session.model.name
      rows.push(row(loadFailed ? 'Model' : 'Error', loadFailed ? failed : 'The turn failed', turn.error.split('\n')[0] ?? '', { tone: 'error' }))
      return { status: Inspector.status(input), rows }
    }
    const usage = turn.result?.usage
    const counts = [usage?.inputTokens === undefined ? undefined : `in ${usage.inputTokens.toLocaleString('en-US')}`, usage?.outputTokens === undefined ? undefined : `out ${usage.outputTokens.toLocaleString('en-US')}`].filter((part) => part !== undefined)
    if (turn.result === undefined) rows.push(row('Usage', status === 'ended' ? 'not reported: the session ended first' : 'reported when the turn ends', '', { tone: 'tertiary' }))
    else if (counts.length > 0) rows.push(row('Usage', counts.join(' · ')))
    else rows.push(row('Usage', session.model.kind === 'subscription' ? `not reported by the ${capitalized(session.model.name)} app` : 'not reported', '', { tone: 'tertiary' }))
    return { status: Inspector.status(input), rows }
  },

  /** The turn's status word and tone: the old prototype's streaming, waiting for you, failed, stopped, done. */
  status({ session, turn }: InspectorInput): { readonly words: string; readonly tone: Tone } {
    switch (session.transcript.status.case) {
      case 'thinking':
      case 'working':
      case 'answering':
        return { words: 'streaming', tone: 'accent' }
      case 'waiting':
        return { words: 'waiting for you', tone: 'warning' }
      case 'stopping':
        return { words: 'stopping', tone: 'accent' }
      case 'restarting':
        return { words: 'restarting', tone: 'accent' }
      case 'ended':
        return { words: 'ended', tone: 'error' }
      default:
        if (turn?.error !== undefined) return { words: 'failed', tone: 'error' }
        if (turn?.result?.finishReason === 'cancelled') return { words: 'stopped', tone: 'tertiary' }
        return { words: turn?.result ? 'done' : 'idle', tone: 'tertiary' }
    }
  },
}
