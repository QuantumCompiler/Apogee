// `apogee models list` and `apogee models status` as documents
// (machine-mode.md, "models list", "models status").
import { decode } from './document'
import type { Decoded } from './document'

/** One row of `models list`: a backend, or a stored model with what runs it. */
export type ModelRow = {
  /** The backend's name, `(not configured)` for a stored set no backend runs. */
  backend: string
  /** `llamacpp`, `mlx`, `anthropic`, ... or `-`. */
  backendType: string
  model: string
  /** `local` or `cloud`: the table's section the row sits in. */
  section: string
  /** Where the row stands: `ok`, `unreadable`, `no key`, ... */
  state: string
  note?: string
  /**
   * What an MLX model is (`chat`, `vision`, `embedding`, `drafter`, `voice`,
   * `video`, ...), where the CLI says (since its 53e); absent on every other row.
   */
  kind?: string
}

/** `models list`: every row. */
export type ModelsList = { rows: ModelRow[] }

/** One `label: value` line of `models status`. */
export type RoleField = { field: string; value: string }

/** `models status`: which backend each role resolves to, in order. */
export type ModelsStatus = { fields: RoleField[] }

/** `models list`'s document, read tolerantly. */
export function decodeModelsList(text: string): Decoded<ModelsList> {
  return decode(text, top => ({
    rows: top.list('data', row => {
      const one: ModelRow = {
        backend: row.text('backend'),
        backendType: row.text('backend_type'),
        model: row.text('model'),
        section: row.text('section'),
        state: row.text('state'),
      }
      const note = row.optionalText('note')
      const kind = row.optionalText('kind')

      return { ...one, ...(note === undefined ? {} : { note }), ...(kind === undefined ? {} : { kind }) }
    }),
  }))
}

/** `models status`'s document, read tolerantly. */
export function decodeModelsStatus(text: string): Decoded<ModelsStatus> {
  return decode(text, top => ({ fields: top.list('fields', field => ({ field: field.text('field'), value: field.text('value') })) }))
}
