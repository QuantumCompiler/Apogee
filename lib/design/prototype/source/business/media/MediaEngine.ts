import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/**
 * What a clip or a speech runs on: the engine its model's directory says
 * (the CLI's 53l, 53m), as the CLI writes it -- `torch` or `mlx`, or any
 * other word a later CLI writes, kept as written. The Mac app's
 * `MediaEngine` (`BusinessMedia`, `MediaEngine.swift`).
 */
export type MediaEngine = string

export const MediaEngine = {
  /** As a person reads it: PyTorch, MLX, or the word as written. */
  name(engine: MediaEngine): string {
    return engine === 'torch' ? 'PyTorch' : engine === 'mlx' ? 'MLX' : engine
  },

  /** The engine a store's video model runs on, by its format and kind as `models list` states them: a pipeline on PyTorch, an MLX directory of kind `video` on MLX; `undefined` for anything else. */
  video(format: string | undefined, kind: string | undefined): MediaEngine | undefined {
    if (format === 'pipeline') return 'torch'
    if (format === 'mlx' && kind === 'video') return 'mlx'
    return undefined
  },

  /** The engine a store's voice model runs on (the CLI's 53m): a `voice` directory on PyTorch, an MLX directory of kind `voice` on MLX; `undefined` for anything else (design 1l). */
  voice(format: string | undefined, kind: string | undefined): MediaEngine | undefined {
    if (format === 'voice') return 'torch'
    if (format === 'mlx' && kind === 'voice') return 'mlx'
    return undefined
  },
}

/** One of the store's video models: its store handle, the engine its directory runs on, and its bytes where the list says. */
export interface MediaModel {
  /** `<model>/<format>/<id>`, what `-m` takes. */
  readonly handle: string
  readonly engine: MediaEngine
  readonly bytes: number | undefined
}

/** The rows of `list` whose format and kind `engineOf` reads as an engine, as models. */
function rowsOf(list: ReadDocument, engineOf: (format: string | undefined, kind: string | undefined) => MediaEngine | undefined): MediaModel[] {
  const found: MediaModel[] = []
  for (const entry of list.entries ?? []) {
    const row = JSONValue.object(entry)
    const handle = JSONValue.string(row?.model)
    const engine = engineOf(JSONValue.string(row?.format), JSONValue.string(row?.kind))
    if (handle === undefined || engine === undefined) continue
    found.push({ handle, engine, bytes: JSONValue.int(row?.bytes) })
  }
  return found
}

function folderOf(model: MediaModel): string {
  return model.handle.split('/')[0] ?? model.handle
}

const rank = (engine: MediaEngine) => (engine === 'torch' ? 0 : engine === 'mlx' ? 1 : 2)

export const MediaModel = {
  /** The model folder: the handle's first part. */
  folder: folderOf,

  /** The store's video models a `models list` document names, in the order a form offers them (`offered`). */
  videoModels(list: ReadDocument): MediaModel[] {
    return MediaModel.offered(rowsOf(list, MediaEngine.video))
  },

  /**
   * The store's voice models a `models list` document names, in the order the
   * say form and the new composition offer them, PyTorch first (the Mac's
   * `MediaModel.readVoiceModels`; design 1l): with two or more, the CLI asks
   * which, so the form names one with `--voice-model`.
   */
  voiceModels(list: ReadDocument): MediaModel[] {
    return MediaModel.offered(rowsOf(list, MediaEngine.voice))
  },

  /** Each folder where the store first lists it, and PyTorch before MLX within a folder: the CLI's `auto` picks neither, so PyTorch comes first (the GUI's 3b). */
  offered(models: readonly MediaModel[]): MediaModel[] {
    const folders: string[] = []
    for (const model of models) if (!folders.includes(folderOf(model))) folders.push(folderOf(model))
    return models
      .map((model, offset) => ({ model, offset }))
      .sort((a, b) => folders.indexOf(folderOf(a.model)) - folders.indexOf(folderOf(b.model)) || rank(a.model.engine) - rank(b.model.engine) || a.offset - b.offset)
      .map(({ model }) => model)
  },

  /** Whether `model`'s folder holds models on more than one engine: then the engine is the person's choice, passed as `--engine`. */
  choosesEngine(model: MediaModel, models: readonly MediaModel[]): boolean {
    return new Set(models.filter((other) => folderOf(other) === folderOf(model)).map((other) => other.engine)).size > 1
  },

  /** How a picker names it among `models`: its folder and engine (`Wan2.2-TI2V-5B · MLX`), or its handle where its folder holds another on the same engine. */
  label(model: MediaModel, models: readonly MediaModel[]): string {
    const twin = models.some((other) => other.handle !== model.handle && folderOf(other) === folderOf(model) && other.engine === model.engine)
    return `${twin ? model.handle : folderOf(model)} · ${MediaEngine.name(model.engine)}`
  },

  /** The model a name given to `-m` names among `models`, as the CLI finds one: its handle, or a folder holding exactly one. */
  named(name: string, models: readonly MediaModel[]): MediaModel | undefined {
    const exact = models.find((model) => model.handle === name)
    if (exact) return exact
    const inFolder = models.filter((model) => folderOf(model) === name)
    return inFolder.length === 1 ? inFolder[0] : undefined
  },
}
