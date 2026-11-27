// `apogee models list` and `apogee models status` as documents
// (machine-mode.md, "models list", "models status").
import type { ApogeeModelRow, ApogeeModelsList, ApogeeModelsStatus, ApogeeRoleField } from '../../../types'
import { decode } from './document'
import type { Decoded } from './document'

/** One row of `models list`: a backend, or a stored model with what runs it (the contract's). */
export type ModelRow = ApogeeModelRow

/** `models list`: every row. */
export type ModelsList = ApogeeModelsList

/** One `label: value` line of `models status`. */
export type RoleField = ApogeeRoleField

/** `models status`: which backend each role resolves to, in order. */
export type ModelsStatus = ApogeeModelsStatus

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
