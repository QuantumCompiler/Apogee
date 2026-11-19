import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/** One `field: value` pair a `fields` document carries (`models info`, `models status`): the Mac app's `Field`. */
export interface Field {
  readonly label: string
  readonly value: string
}

export const Field = {
  /** A document's `fields`, in order: `[{"field": …, "value": …}]`; one with no label is skipped. */
  list(document: ReadDocument): Field[] {
    return (JSONValue.array(document.value.fields) ?? []).flatMap((entry) => {
      const object = JSONValue.object(entry)
      const label = JSONValue.string(object?.field)
      const value = JSONValue.string(object?.value)
      return label === undefined || label === '' || value === undefined ? [] : [{ label, value }]
    })
  },
}

/**
 * Where a model runs, as the design marks it: on this machine, a cloud API
 * billed per use, or a vendor's app on a subscription. The Mac app's
 * `ModelKind` (`BusinessReads`, `Chats.swift`).
 */
export type ModelKind = 'local' | 'api' | 'subscription'

export const ModelKind = {
  /** The kind a backend's `type` is; `undefined` for one that is none of them (the mock backend) or a type this build does not know -- never guessed. */
  of(backendType: string): ModelKind | undefined {
    switch (backendType) {
      case 'llamacpp':
      case 'mlx':
      case 'ollama-cli':
        return 'local'
      case 'anthropic':
      case 'openai':
      case 'google':
        return 'api'
      case 'claude-cli':
      case 'codex-cli':
      case 'gemini-cli':
        return 'subscription'
      default:
        return undefined
    }
  },

  /** Each configured backend's kind, from a `models list` document's rows. */
  byBackend(document: ReadDocument): Readonly<Record<string, ModelKind>> {
    const kinds: Record<string, ModelKind> = {}
    for (const row of ModelRow.list(document)) {
      const kind = ModelKind.of(row.backendType)
      if (kind) kinds[row.backend] = kind
    }
    return kinds
  },
}

/**
 * One row of `models list --output-format json`: a configured backend and
 * its model, or stored weights. The Mac app's `ModelRow` (`Models.swift`),
 * the fields the session's model menu reads.
 */
export interface ModelRow {
  /** The backend's name -- what `models info`, `-m` and `set` take. */
  readonly backend: string
  readonly backendType: string
  readonly model: string
  readonly state: string
  /** `local` or `cloud`, and the table's group in it (`gguf`, `anthropic` …). */
  readonly section: string
  readonly group: string
  readonly note: string | undefined
  /** An MLX directory's kind, as the row states it (the CLI's 53e); `undefined` on every other row. */
  readonly mlxKind: string | undefined
}

export const ModelRow = {
  /** The backend `models list` writes for stored weights no backend points at. */
  unconfigured: '(not configured)',

  /** Every row of a `models list` document, in its order; one with no backend is skipped. */
  list(document: ReadDocument): ModelRow[] {
    return (document.entries ?? []).flatMap((entry) => {
      const object = JSONValue.object(entry)
      const text = (key: string) => JSONValue.string(object?.[key]) ?? ''
      const optional = (key: string) => {
        const value = JSONValue.string(object?.[key])
        return value === undefined || value === '' ? undefined : value
      }
      const backend = text('backend')
      if (backend === '') return []
      return [
        {
          backend,
          backendType: text('backend_type'),
          model: text('model'),
          state: text('state'),
          section: optional('section') ?? 'local',
          group: optional('group') ?? 'other',
          note: optional('note'),
          mlxKind: optional('kind'),
        },
      ]
    })
  },

  /** Stored weights no backend points at: no backend at all. */
  isStored(row: ModelRow): boolean {
    return row.backend === ModelRow.unconfigured && row.backendType === '-'
  },

  /** Whether a chat runs on it: an MLX row of kind `chat` or `vision`, or a row with no kind (the GUI's M4). */
  chats(row: ModelRow): boolean {
    return row.mlxKind === undefined || row.mlxKind === 'chat' || row.mlxKind === 'vision'
  },

  /** The backends a chat can be offered: each configured backend that chats, once, in the list's order. */
  chatBackends(rows: readonly ModelRow[]): string[] {
    const names: string[] = []
    for (const row of rows) {
      if (!ModelRow.isStored(row) && ModelRow.chats(row) && !names.includes(row.backend)) names.push(row.backend)
    }
    return names
  },
}

/** What `models info <model>` states. */
export const ModelInfo = {
  /**
   * The window of a model, for the context meter: a stored set's typed
   * `data.window.tokens` (the CLI's 52e), else the leading count of its
   * `window` field ("8192 tokens (the default; …)"). An API model states
   * none: `undefined`, never guessed (the Mac app's `SessionLibraryViewModel.window(in:)`).
   */
  window(document: ReadDocument): number | undefined {
    const tokens = JSONValue.int(JSONValue.object(JSONValue.object(document.value.data)?.window)?.tokens)
    if (tokens !== undefined && tokens > 0) return tokens
    const text = Field.list(document).find((field) => field.label === 'window')?.value
    const digits = text === undefined ? undefined : /^\d+/.exec(text)?.[0]
    const count = digits === undefined ? undefined : Number(digits)
    return count !== undefined && count > 0 ? count : undefined
  },
}
