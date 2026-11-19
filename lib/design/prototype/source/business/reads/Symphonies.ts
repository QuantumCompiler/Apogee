import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/** A symphony as `symphonies list` names it: what `/play` offers (the Mac app's `Symphony`, its name and description). */
export interface Symphony {
  readonly name: string
  readonly description: string
}

export const Symphony = {
  /** Each symphony of a `symphonies list` document, in its order; one with no name is skipped. */
  list(document: ReadDocument): Symphony[] {
    return (document.entries ?? []).flatMap((entry) => {
      const object = JSONValue.object(entry)
      const name = JSONValue.string(object?.name)
      return name === undefined || name === '' ? [] : [{ name, description: JSONValue.string(object?.description) ?? '' }]
    })
  },
}
