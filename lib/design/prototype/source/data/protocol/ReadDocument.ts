import { JSONValue } from './JSONValue'
import type { JSONObject } from './JSONValue'

/**
 * One document a read printed, `apogee <read> --output-format json`: the Mac
 * app's `ReadDocument` (`DataProtocol`, `ReadDocument.swift`). The document
 * is kept whole; the accessors read the shapes the reads share.
 */
export class ReadDocument {
  private constructor(readonly value: JSONObject) {}

  /** The document `text` holds, or `undefined` when it is not one JSON object. */
  static from(text: string): ReadDocument | undefined {
    const value = JSONValue.object(JSONValue.parse(text))
    return value ? new ReadDocument(value) : undefined
  }

  /** The document's `object`: `list`, `task.recipe`, `graph.path` … */
  get object(): string | undefined {
    return JSONValue.string(this.value.object)
  }

  /** A list's entries (`{"object":"list","data":[…]}`). */
  get entries(): readonly JSONValue[] | undefined {
    return JSONValue.array(this.value.data)
  }

  /** How many there are in all, when the list says (it may show fewer). */
  get total(): number | undefined {
    return JSONValue.int(this.value.total)
  }

  /** What the read could not parse, named by it. */
  get problems(): readonly JSONValue[] {
    return JSONValue.array(this.value.problems) ?? []
  }
}
