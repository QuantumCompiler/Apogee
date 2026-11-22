// How the CLI's documents are read: tolerantly (the client invariant; the
// CLI's ADR 0006 consumed). An unknown field is ignored, a missing one is
// said and read as unknown, and text that is no document is a stated
// condition. Nothing here throws.

/** A document read: its value and the fields it lacked, or why it could not be read. */
export type Decoded<T> = { kind: 'read'; value: T; missing: string[] } | { kind: 'unreadable'; reason: string }

/** A JSON object's fields, as plain data. */
export type Fields = Readonly<Record<string, unknown>>

/** The JSON object `text` holds, or why it holds none. */
export function objectOf(text: string): { kind: 'object'; fields: Fields } | { kind: 'unreadable'; reason: string } {
  const trimmed = text.trim()
  if (trimmed === '') {
    return { kind: 'unreadable', reason: 'an empty document' }
  }
  let parsed: unknown
  try {
    parsed = JSON.parse(trimmed)
  } catch {
    return { kind: 'unreadable', reason: `not a JSON document: "${opening(trimmed)}"` }
  }
  if (!isFields(parsed)) {
    return { kind: 'unreadable', reason: `not a JSON object: "${opening(trimmed)}"` }
  }

  return { kind: 'object', fields: parsed }
}

/** Whether `value` is a JSON object (not an array, not null). */
export function isFields(value: unknown): value is Fields {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

/**
 * Reads the fields of one object, noting each expected one it lacks under
 * its path (`rows[3].status`): a field of the wrong type is lacking too.
 */
export type Reader = {
  /** A string field; '' and said when absent. */
  text: (key: string) => string
  /** A string field that may be absent or null without being said. */
  optionalText: (key: string) => string | undefined
  /** A string field that may be null (none) without being said; said when absent. */
  nullableText: (key: string) => string | undefined
  /** A number field; undefined and said when absent. */
  number: (key: string) => number | undefined
  /** A number field that may be absent without being said. */
  optionalNumber: (key: string) => number | undefined
  /** A boolean field; undefined and said when absent. */
  flag: (key: string) => boolean | undefined
  /** A boolean field that may be absent without being said. */
  optionalFlag: (key: string) => boolean | undefined
  /** An array field's objects, each read by `read`; [] and said when absent. */
  list: <T>(key: string, read: (one: Reader) => T) => T[]
  /** A field's value as it is, unread; undefined when absent, never said. */
  raw: (key: string) => unknown
  /** An array field's strings; [] and said when absent. */
  texts: (key: string) => string[]
  /** An object field, read by `read`; undefined when absent or null, never said. */
  optionalObject: <T>(key: string, read: (one: Reader) => T) => T | undefined
  /** Says a field lacking that no one read could: one of several, none there (`gguf_path|mlx_path`). */
  lack: (key: string) => void
}

/** A reader over `fields`, at `at`, noting what it lacks in `missing`. */
export function readerOf(fields: Fields, at: string, missing: string[]): Reader {
  const path = (key: string): string => (at === '' ? key : `${at}.${key}`)
  const lacking = <T>(key: string, value: T): T => {
    missing.push(path(key))

    return value
  }
  const isNumber = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value)

  return {
    text: key => {
      const value = fields[key]

      return typeof value === 'string' ? value : lacking(key, '')
    },
    optionalText: key => {
      const value = fields[key]

      return typeof value === 'string' ? value : undefined
    },
    nullableText: key => {
      const value = fields[key]

      return typeof value === 'string' ? value : value === null ? undefined : lacking(key, undefined)
    },
    number: key => {
      const value = fields[key]

      return isNumber(value) ? value : lacking(key, undefined)
    },
    optionalNumber: key => {
      const value = fields[key]

      return isNumber(value) ? value : undefined
    },
    flag: key => {
      const value = fields[key]

      return typeof value === 'boolean' ? value : lacking(key, undefined)
    },
    optionalFlag: key => {
      const value = fields[key]

      return typeof value === 'boolean' ? value : undefined
    },
    list: (key, read) => {
      const value = fields[key]
      if (!Array.isArray(value)) {
        return lacking(key, [])
      }

      return value.flatMap((one, index) => (isFields(one) ? [read(readerOf(one, `${path(key)}[${index}]`, missing))] : []))
    },
    raw: key => fields[key],
    texts: key => {
      const value = fields[key]

      return Array.isArray(value) ? value.filter(one => typeof one === 'string') : lacking(key, [])
    },
    optionalObject: (key, read) => {
      const value = fields[key]

      return isFields(value) ? read(readerOf(value, path(key), missing)) : undefined
    },
    lack: key => {
      lacking(key, undefined)
    },
  }
}

/** Decodes `text` as one document whose top-level object `read` reads. */
export function decode<T>(text: string, read: (top: Reader) => T): Decoded<T> {
  const object = objectOf(text)
  if (object.kind === 'unreadable') {
    return object
  }
  const missing: string[] = []
  const value = read(readerOf(object.fields, '', missing))

  return { kind: 'read', value, missing }
}

/** The first line of `text`, cut short, as a reason quotes it. */
function opening(text: string): string {
  const first = text.split('\n')[0] ?? ''

  return first.length > 60 ? `${first.slice(0, 60)}…` : first
}
