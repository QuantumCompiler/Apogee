/**
 * Any JSON value, kept exactly as it arrived: the Mac app's `JSONValue`
 * (`DataProtocol`, `JSONValue.swift`). The protocol grows by addition, so a
 * client keeps what it does not understand rather than dropping it.
 */
export type JSONValue = null | boolean | number | string | readonly JSONValue[] | JSONObject

export interface JSONObject {
  readonly [key: string]: JSONValue
}

export const JSONValue = {
  /** One JSON text, or `undefined` when it is not one. */
  parse(text: string): JSONValue | undefined {
    try {
      return JSON.parse(text) as JSONValue
    } catch {
      return undefined
    }
  },

  string(value: JSONValue | undefined): string | undefined {
    return typeof value === 'string' ? value : undefined
  },

  bool(value: JSONValue | undefined): boolean | undefined {
    return typeof value === 'boolean' ? value : undefined
  },

  number(value: JSONValue | undefined): number | undefined {
    return typeof value === 'number' ? value : undefined
  },

  /** The number, when it is a whole one, as `intValue` reads it (under 1e15). */
  int(value: JSONValue | undefined): number | undefined {
    return typeof value === 'number' && Number.isInteger(value) && Math.abs(value) < 1e15 ? value : undefined
  },

  array(value: JSONValue | undefined): readonly JSONValue[] | undefined {
    return Array.isArray(value) ? (value as readonly JSONValue[]) : undefined
  },

  object(value: JSONValue | undefined): JSONObject | undefined {
    return typeof value === 'object' && value !== null && !Array.isArray(value) ? (value as JSONObject) : undefined
  },

  /** `text` as a JSON string literal, quotes included. */
  quoted(text: string): string {
    return JSON.stringify(text)
  },
}
