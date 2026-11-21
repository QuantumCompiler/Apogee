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
 * What an MLX directory is, as the CLI writes the word (its 53e): `chat`,
 * `vision`, `embedding`, `drafter`, `video`, `voice`, or a later CLI's word,
 * carried as written. The Mac app's `MLXKind`.
 */
export type MLXKind = string

export const MLXKind = {
  /** Whether an `mlx` backend chats with it: only `chat` and `vision`. */
  chats(kind: MLXKind): boolean {
    return kind === 'chat' || kind === 'vision'
  },
  /** Whether an `mlx` entry runs it at all: chats with it, or embeds with it (53e, 53h). */
  registers(kind: MLXKind): boolean {
    return MLXKind.chats(kind) || kind === 'embedding'
  },
  /** The kind an MLX set reads as: the one stated, else a chat model's, as before kinds existed. */
  reading(stated: MLXKind | undefined): MLXKind {
    return stated ?? 'chat'
  },
}

/** An `mlx` entry's drafter, as `models list` states it (the CLI's M32). */
export interface RowDraft {
  readonly path: string
  readonly canDraft: boolean
  /** Why it cannot draft there, in the CLI's words; `undefined` where it drafts. */
  readonly refusal: string | undefined
}

/**
 * One row of `models list --output-format json`: a configured backend and
 * its model, or stored weights. The Mac app's `ModelRow` (`Models.swift`).
 */
export interface ModelRow {
  /** The backend's name -- what `models info`, `-m` and `set` take; `(not configured)` for stored weights. */
  readonly backend: string
  readonly backendType: string
  /** The model: a stored set's handle (`<model>/<format>/<id>`), else its file or the provider's name. */
  readonly model: string
  /** The roles it holds, as the CLI writes them (`chat, embedding`). */
  readonly roles: string
  readonly source: string
  readonly state: string
  /** `local` or `cloud`, and the table's group in it (`gguf`, `anthropic` …). */
  readonly section: string
  readonly group: string
  readonly note: string | undefined
  readonly format: string | undefined
  readonly quant: string | undefined
  /** An MLX directory's kind, as the row states it (the CLI's 53e); `undefined` on every other row. */
  readonly mlxKind: MLXKind | undefined
  /** The drafter an `mlx` entry names (the CLI's M32), when its row states one. */
  readonly draft: RowDraft | undefined
  /** The name the CLI's verbs take: a backend's, or stored weights' handle. */
  readonly name: string
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
      const backendType = text('backend_type')
      const model = text('model')
      const stored = backend === ModelRow.unconfigured && backendType === '-'
      const name = stored ? model : backend
      if (name === '') return []
      return [
        {
          backend,
          backendType,
          model,
          roles: text('roles'),
          source: text('source'),
          state: text('state'),
          section: optional('section') ?? 'local',
          group: optional('group') ?? 'other',
          note: optional('note'),
          format: optional('format'),
          quant: optional('quant'),
          mlxKind: optional('kind'),
          draft: ModelRow.draft(object?.draft),
          name,
        },
      ]
    })
  },

  /** A row's `draft`, where it states one with its path. */
  draft(value: JSONValue | undefined): RowDraft | undefined {
    const object = JSONValue.object(value ?? null)
    const path = JSONValue.string(object?.path)
    if (path === undefined || path === '') return undefined
    const said = JSONValue.string(object?.refusal)
    const refusal = said === undefined || said === '' ? undefined : said
    const can = object?.can_draft
    return { path, canDraft: typeof can === 'boolean' ? can : refusal === undefined, refusal }
  },

  /** How many stored sets `models list` folded away -- those a conversion consumed -- which `--all` lists. */
  folded(document: ReadDocument): number {
    return JSONValue.int(document.value.folded) ?? 0
  },

  /** Stored weights no backend points at: no backend at all. */
  isStored(row: ModelRow): boolean {
    return row.backend === ModelRow.unconfigured && row.backendType === '-'
  },

  /** Whether a chat runs on it: an MLX row of kind `chat` or `vision`, or a row with no kind (the GUI's M4). */
  chats(row: ModelRow): boolean {
    return row.mlxKind === undefined || MLXKind.chats(row.mlxKind)
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

/** One step back in where a set came from: `pulled`, `converted`, `quantized`, `trained`. */
export interface LineageStep {
  readonly kind: string
  readonly from: string | undefined
  readonly detail: string | undefined
}

/** A set's typed facts, since the CLI's 52e: each `undefined` where its format's reader states nothing. */
export interface ModelFacts {
  readonly format: string | undefined
  readonly handle: string | undefined
  readonly bytes: number | undefined
  readonly architecture: string | undefined
  readonly window: { readonly tokens: number; readonly source: string | undefined; readonly trained: number | undefined } | undefined
  readonly cache: { readonly bytes: number; readonly type: string | undefined } | undefined
  readonly lineage: readonly LineageStep[]
  /** A SafeTensors set's conversions, as the CLI decides them (M20); `undefined` where the document does not say. */
  readonly convertsTo: readonly string[] | undefined
  /** An MLX set's kind (53e). */
  readonly kind: MLXKind | undefined
}

/** `models info <name> --output-format json`, read whole: its record and, for a stored set, its typed facts. */
export interface ModelInfoDocument {
  readonly name: string
  readonly fields: readonly Field[]
  readonly data: ModelFacts | undefined
}

/** What `models info <model>` states: the Mac app's `ModelInfo`. */
export const ModelInfo = {
  /** The document whole: its `name`, its `label: value` record and its typed `data` where it has one. */
  read(document: ReadDocument): ModelInfoDocument {
    const data = JSONValue.object(document.value.data ?? null)
    const int = JSONValue.int
    const text = JSONValue.string
    let facts: ModelFacts | undefined
    if (data) {
      const window = JSONValue.object(data.window ?? null)
      const tokens = int(window?.tokens)
      const cache = JSONValue.object(data.cache ?? null)
      const cacheBytes = int(cache?.bytes)
      const converts = JSONValue.array(data.converts_to ?? null)
      facts = {
        format: text(data.format),
        handle: text(data.handle),
        bytes: int(data.bytes),
        architecture: text(data.architecture),
        window: tokens === undefined ? undefined : { tokens, source: text(window?.source), trained: int(window?.trained) },
        cache: cacheBytes === undefined ? undefined : { bytes: cacheBytes, type: text(cache?.type) },
        lineage: (JSONValue.array(data.lineage ?? null) ?? []).flatMap((entry) => {
          const step = JSONValue.object(entry)
          const kind = text(step?.kind)
          return kind === undefined ? [] : [{ kind, from: text(step?.from), detail: text(step?.detail) }]
        }),
        convertsTo: converts === undefined ? undefined : converts.flatMap((value) => (typeof value === 'string' ? [value] : [])),
        kind: text(data.kind),
      }
    }
    return { name: text(document.value.name) ?? '', fields: Field.list(document), data: facts }
  },

  /** A field's value by its label. */
  field(info: ModelInfoDocument, label: string): string | undefined {
    return info.fields.find((field) => field.label === label)?.value
  },

  /** Memory a conversation holds at the window: the weights and the cache, when the document states both -- never an estimate. */
  memory(info: ModelInfoDocument): number | undefined {
    const bytes = info.data?.bytes
    const cache = info.data?.cache?.bytes
    return bytes === undefined || cache === undefined ? undefined : bytes + cache
  },

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

/** A role and the backend it resolves to, as `models status` says it: the Mac app's `RoleAssignment`. */
export interface RoleAssignment {
  /** `chat`, `embedding`, `extraction`, `vision`, `transcription`, `utility`. */
  readonly role: string
  /** The CLI's words: `fastroot   (via suite fast)`, `(unset -- the chat's own backend)`. */
  readonly said: string
}

export const RoleAssignment = {
  /** The roles `models status` resolves, in its order: never its `suite` or `footprint` field (GUI 6d). */
  resolved: ['chat', 'embedding', 'extraction', 'vision', 'transcription', 'utility'] as const,

  list(document: ReadDocument): RoleAssignment[] {
    return Field.list(document)
      .filter((field) => (RoleAssignment.resolved as readonly string[]).includes(field.label))
      .map((field) => ({ role: field.label, said: field.value }))
  },

  /** The backend it points at, when the CLI names one first. */
  backend(role: RoleAssignment): string | undefined {
    const first = role.said.split(' ')[0] ?? ''
    return first === '' || first.startsWith('(') ? undefined : first
  },

  /** Whether the role names its backend itself rather than falling back. */
  isSet(role: RoleAssignment): boolean {
    return RoleAssignment.backend(role) !== undefined && !role.said.includes('(via ')
  },

  /** The verb that points the role at a backend: `config set-default-vision`; the chat role's is `config set-default`. */
  verb(role: RoleAssignment): string[] {
    return role.role === 'chat' ? ['config', 'set-default'] : ['config', `set-default-${role.role}`]
  },

  /** The suite the roles resolve under, as `models status` names it; `undefined` with none active. */
  suite(document: ReadDocument): string | undefined {
    const said = Field.list(document).find((field) => field.label === 'suite')?.value
    if (said === undefined) return undefined
    const at = said.indexOf('   (')
    const name = at >= 0 ? said.slice(0, at) : said
    return name === '' ? undefined : name
  },
}
