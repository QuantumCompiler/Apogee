import { JSONValue } from './JSONValue'

/** The run settings a session moves: what `-m`, `-s`, `--think`, `--effort` and `-t` choose at launch, and `set` moves after. */
export interface RunSettings {
  /** A backend, a roster model or `backend:model`. */
  readonly model?: string
  /** The system prompt, replaced whole. */
  readonly system?: string
  /** `on`, `off` or `auto`. */
  readonly think?: string
  /** `low` … `max`, or `auto`. */
  readonly effort?: string
  readonly temperature?: number
}

/**
 * A line the client writes to a session's stdin: the Mac app's `DriverLine`
 * (`DataProtocol`, `DriverLine.swift`), one JSON object a line, its `type`
 * first and its other fields in a fixed order, so the same line is always the
 * same bytes.
 */
export type DriverLine =
  | { readonly case: 'hello'; readonly clientName: string; readonly clientVersion: string; readonly wants: readonly string[] }
  | { readonly case: 'user'; readonly text: string }
  | { readonly case: 'answer'; readonly text: string }
  | { readonly case: 'attach'; readonly path: string; readonly graph?: string }
  | { readonly case: 'cancel' }
  | { readonly case: 'detach'; readonly path: string }
  | { readonly case: 'set'; readonly settings: RunSettings }
  | { readonly case: 'compact' }

export const DriverLine = {
  /** The line as written, with no newline: the writer adds it. */
  encoded(line: DriverLine): string {
    const q = JSONValue.quoted
    const object = (fields: ReadonlyArray<readonly [string, string]>) =>
      `{${fields.map(([key, value]) => `${q(key)}:${value}`).join(',')}}`
    switch (line.case) {
      case 'hello':
        return object([
          ['type', q('hello')],
          ['client', object([['name', q(line.clientName)], ['version', q(line.clientVersion)]])],
          ['wants', `[${line.wants.map(q).join(',')}]`],
        ])
      case 'user':
      case 'answer':
        return object([['type', q(line.case)], ['text', q(line.text)]])
      case 'attach':
        return object([['type', q('attach')], ['path', q(line.path)], ...(line.graph === undefined ? [] : [['graph', q(line.graph)] as const])])
      case 'cancel':
      case 'compact':
        return object([['type', q(line.case)]])
      case 'detach':
        return object([['type', q('detach')], ['path', q(line.path)]])
      case 'set':
        return object([['type', q('set')], ...RunSettings.fields(line.settings)])
    }
  },
}

export const RunSettings = {
  /** The set ones, in the order the CLI applies them: the model first. */
  fields(settings: RunSettings): Array<readonly [string, string]> {
    const fields: Array<readonly [string, string]> = []
    if (settings.model !== undefined) fields.push(['model', JSONValue.quoted(settings.model)])
    if (settings.system !== undefined) fields.push(['system', JSONValue.quoted(settings.system)])
    if (settings.think !== undefined) fields.push(['think', JSONValue.quoted(settings.think)])
    if (settings.effort !== undefined) fields.push(['effort', JSONValue.quoted(settings.effort)])
    if (settings.temperature !== undefined) fields.push(['temperature', String(settings.temperature)])
    return fields
  },
}
