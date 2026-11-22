// `apogee check --output-format json` (machine-mode.md, "check"): every row
// the report shows, with its status (ok, warn, fail, skipped), and the counts.
import { decode } from './document'
import type { Decoded } from './document'

/** One row of the install check. */
export type CheckRow = {
  section: string
  name: string
  /** `ok`, `warn`, `fail` or `skipped`, as the CLI says it. */
  status: string
  detail: string
  /** The command or step that mends it, when the row names one. */
  remedy?: string
}

/** The install check's report. */
export type CheckReport = {
  rows: CheckRow[]
  /** The verdict: no failure. */
  isOk?: boolean
  failures?: number
  warnings?: number
}

/** The check's document, read tolerantly. */
export function decodeCheck(text: string): Decoded<CheckReport> {
  return decode(text, top => {
    const rows = top.list('rows', row => {
      const one: CheckRow = { section: row.text('section'), name: row.text('name'), status: row.text('status'), detail: row.text('detail') }
      const remedy = row.optionalText('remedy')

      return remedy === undefined ? one : { ...one, remedy }
    })
    const isOk = top.flag('ok')
    const failures = top.number('failures')
    const warnings = top.number('warnings')

    return {
      rows,
      ...(isOk === undefined ? {} : { isOk }),
      ...(failures === undefined ? {} : { failures }),
      ...(warnings === undefined ? {} : { warnings }),
    }
  })
}
