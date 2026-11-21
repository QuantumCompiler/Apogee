import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/** One stage of a symphony, in play order: the role it asks, or the symphony it plays (the Mac app's `Symphony.Stage`). */
export interface SymphonyStage {
  readonly name: string
  /** The role it asks (`utility`, `chat` ...); `undefined` for a stage that plays another symphony. */
  readonly role: string | undefined
  readonly plays: string | undefined
  readonly prompt: string
}

/**
 * A symphony, as `symphonies list | show --output-format json` carries it:
 * the Mac app's `Symphony` (`Library.swift`). `/play` (1u) reads its name and
 * description; the Library (1n) reads it whole -- its source, its file, its
 * input, its stages in play order, and anything wrong with it.
 */
export interface Symphony {
  readonly name: string
  readonly description: string
  /** `shipped` (a seeded starter), `config` (an entry, no file) or `file` (the person's own spec file). */
  readonly source: string
  readonly path: string | undefined
  readonly inputDescription: string
  /** Its input takes an image (`input.image`): only the symphony's own document says so (GUI 6f). */
  readonly takesImage: boolean
  readonly stages: readonly SymphonyStage[]
  readonly problems: readonly string[]
}

export const Symphony = {
  /** One symphony's document, or `undefined` with no name. */
  read(value: JSONValue): Symphony | undefined {
    const object = JSONValue.object(value)
    const name = JSONValue.string(object?.name)
    if (!object || name === undefined || name === '') return undefined
    const input = JSONValue.object(object.input)
    const path = JSONValue.string(object.path)
    return {
      name,
      description: JSONValue.string(object.description) ?? '',
      source: JSONValue.string(object.source) ?? '',
      path: path === '' ? undefined : path,
      inputDescription: JSONValue.string(input?.description) ?? '',
      takesImage: JSONValue.bool(input?.image) ?? false,
      stages: (JSONValue.array(object.stages) ?? []).flatMap((entry): SymphonyStage[] => {
        const stage = JSONValue.object(entry)
        const stageName = JSONValue.string(stage?.name)
        if (!stage || stageName === undefined) return []
        const plays = JSONValue.string(stage.play) ?? JSONValue.string(JSONValue.object(stage.play)?.symphony)
        return [{ name: stageName, role: JSONValue.string(stage.role), plays, prompt: JSONValue.string(stage.prompt) ?? '' }]
      }),
      problems: (JSONValue.array(object.problems) ?? []).flatMap((problem) => {
        const said = JSONValue.string(problem) ?? JSONValue.string(JSONValue.object(problem)?.message)
        return said === undefined ? [] : [said]
      }),
    }
  },

  /** Each symphony of a `symphonies list` document, in its order; one with no name is skipped. */
  list(document: ReadDocument): Symphony[] {
    return (document.entries ?? []).flatMap((entry) => Symphony.read(entry) ?? [])
  },

  /** A spec file of the person's own: what the app edits in place. */
  isOwnFile(symphony: Symphony): boolean {
    return symphony.source === 'file' && symphony.path !== undefined
  },
}

/** One stage's answer in a play: the Mac app's `SymphonyPlay.Stage`. */
export interface SymphonyPlayStage {
  readonly name: string
  readonly role: string
  readonly backend: string
  readonly answer: string
}

/** `symphonies play --output-format json`: each stage's answer, the backend that gave it, and the play's output (the Mac app's `SymphonyPlay`). */
export interface SymphonyPlay {
  readonly symphony: string
  readonly output: string
  readonly stages: readonly SymphonyPlayStage[]
}

export const SymphonyPlay = {
  /** The play's document, or `undefined` when the document is another object. */
  read(document: ReadDocument): SymphonyPlay | undefined {
    if (document.object !== 'symphony.play') return undefined
    const value = document.value
    return {
      symphony: JSONValue.string(value.symphony) ?? '',
      output: JSONValue.string(value.output) ?? '',
      stages: (JSONValue.array(value.stages) ?? []).flatMap((entry): SymphonyPlayStage[] => {
        const stage = JSONValue.object(entry)
        const name = JSONValue.string(stage?.name)
        if (!stage || name === undefined) return []
        return [{ name, role: JSONValue.string(stage.role) ?? '', backend: JSONValue.string(stage.backend) ?? '', answer: JSONValue.string(stage.answer) ?? '' }]
      }),
    }
  },
}
