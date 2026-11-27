import { JSONValue } from '../../data/protocol/JSONValue'

/**
 * An adapter a backend applies over its `model_path`, never merged into it
 * (the CLI's 53a): one line of `config get backends.<name>.adapters` -- its
 * path, and its scale after the last colon when it is not 1. The Mac app's
 * `BackendAdapter` (`BusinessReads`, `Adapters.swift`).
 */
export interface BackendAdapter {
  readonly path: string
  /** In (0, 2]; 1 unless the line says otherwise. */
  readonly scale: number
}

export const BackendAdapter = {
  /** `<path>[:<scale>]`: only a number after the last colon is a scale, as the CLI reads it. */
  line(line: string): BackendAdapter {
    const colon = line.lastIndexOf(':')
    const scale = colon >= 0 ? Number(line.slice(colon + 1)) : Number.NaN
    return colon >= 0 && line.slice(colon + 1).trim() !== '' && Number.isFinite(scale) ? { path: line.slice(0, colon), scale } : { path: line, scale: 1 }
  },

  /** Every adapter `config get backends.<name>.adapters` printed, one per line; none for an empty answer. */
  list(text: string): BackendAdapter[] {
    return text
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter((line) => line !== '')
      .map(BackendAdapter.line)
  },

  /** Its last path component: the name `check`'s row gives it. */
  file(adapter: BackendAdapter): string {
    const trimmed = adapter.path.endsWith('/') ? adapter.path.slice(0, -1) : adapter.path
    return trimmed.split('/').at(-1) ?? trimmed
  },

  /** As `--adapters` takes it: the path, then `:<scale>` unless it is 1. */
  argument(adapter: BackendAdapter): string {
    return adapter.scale === 1 ? adapter.path : `${adapter.path}:${adapter.scale}`
  },
}

/** What `check`'s Adapters section says of one adapter (the CLI's 53c): the Mac app's `AdapterCheck`. */
export interface AdapterCheck {
  /** `ok`, `warn`, `fail` or `skipped`. */
  readonly status: string
  readonly detail: string
  readonly remedy: string | undefined
}

export const AdapterCheck = {
  /** Each adapter's row in `check --output-format json`'s document, by its path: rows are named `<backend>: <file>`, in order. */
  rows(adapters: readonly BackendAdapter[], backend: string, check: JSONValue): Record<string, AdapterCheck> {
    const rows = (JSONValue.array(JSONValue.object(check)?.rows ?? null) ?? []).filter((row) => JSONValue.string(JSONValue.object(row)?.section) === 'Adapters')
    const found: Record<string, AdapterCheck> = {}
    for (const adapter of adapters) {
      const index = rows.findIndex((row) => JSONValue.string(JSONValue.object(row)?.name) === `${backend}: ${BackendAdapter.file(adapter)}`)
      if (index < 0) continue
      const row = JSONValue.object(rows.splice(index, 1)[0] ?? null)
      found[adapter.path] = { status: JSONValue.string(row?.status) ?? '', detail: JSONValue.string(row?.detail) ?? '', remedy: JSONValue.string(row?.remedy) }
    }
    return found
  },
}
