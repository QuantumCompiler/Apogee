import type { ReadDocument } from '../../data/protocol/ReadDocument'
import { JSONValue } from '../../data/protocol/JSONValue'

/** One row of `check --output-format json`: the Mac app's `SettingsViewModel.CheckRow`. */
export interface CheckRow {
  readonly section: string
  readonly name: string
  /** `ok`, `warn`, `fail`, `skipped`. */
  readonly status: string
  readonly detail: string
  /** The command that mends it, as the CLI writes it; only on a row that names one. */
  readonly remedy: string | undefined
}

/** The install `check` describes, from its Version rows, in its words; `undefined` where it printed none. */
export interface CheckInstall {
  readonly version: string | undefined
  readonly channel: string | undefined
  /** The data folder, its root row's path before ` -- ` (which rung chose it). */
  readonly root: string | undefined
}

/**
 * What `check` printed (design 1i): every row, in its order, and its counts
 * -- the document stands whatever the exit, since `check` exits non-zero
 * when a row fails and its document says which (`CommandRunning.report`).
 */
export interface CheckReport {
  readonly rows: readonly CheckRow[]
  readonly failures: number
  readonly warnings: number
  /** What Health draws, in order: what fails, what to look at, then each row skipped with a remedy (the Mac's `healthRows`). */
  readonly health: readonly CheckRow[]
  /** `Everything checks out`, or `<n> failing, <m> to look at`: the Mac's words. */
  readonly summary: string
  readonly install: CheckInstall
}

export const CheckReport = {
  of(document: ReadDocument): CheckReport {
    const rows = (JSONValue.array(document.value.rows) ?? []).flatMap((entry): CheckRow[] => {
      const row = JSONValue.object(entry)
      if (!row) return []
      const text = (key: string) => JSONValue.string(row[key]) ?? ''
      const remedy = JSONValue.string(row.remedy)
      return [{ section: text('section'), name: text('name'), status: text('status'), detail: text('detail'), remedy: remedy === undefined || remedy === '' ? undefined : remedy }]
    })
    const failures = JSONValue.int(document.value.failures) ?? 0
    const warnings = JSONValue.int(document.value.warnings) ?? 0
    const health = [...rows.filter((row) => row.status === 'fail'), ...rows.filter((row) => row.status === 'warn'), ...rows.filter((row) => row.status === 'skipped' && row.remedy !== undefined)]
    const version = (name: string) => rows.find((row) => row.section === 'Version' && row.name === name)?.detail
    const root = version('root')
    return {
      rows,
      failures,
      warnings,
      health,
      summary: failures === 0 && warnings === 0 ? 'Everything checks out' : `${failures} failing, ${warnings} to look at`,
      install: { version: version('apogee'), channel: version('channel'), root: root === undefined ? undefined : (root.split(' -- ')[0] ?? root) },
    }
  },
}
