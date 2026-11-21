import { JSONValue } from '../../data/protocol/JSONValue'
import type { FolderFile, FolderReading } from '../../infrastructure/process/StandInFolders'

export type { FolderFile, FolderReading }

/** A LoRA a run applied (the CLI's 49e): its file, its strength, and the sha256 a rerun checks it by. */
export interface ClipLoRA {
  readonly path: string
  readonly scale: number | undefined
  readonly sha256: string | undefined
}

/** Where a clip ran, as its record says: torch's device and type (or MLX's), and the build's target. */
export interface ClipDevice {
  readonly device: string | undefined
  readonly dtype: string | undefined
  readonly platform: string | undefined
}

/**
 * A clip's record: the `video.generation` document a run writes beside its
 * clip as `<clip>.json`, the one `video generate --output-format json`
 * prints and its job stream closes on (the CLI's 49c-49e). The Mac app's
 * `ClipRecord` (`BusinessMedia`, `MediaRecords.swift`), with the fields the
 * design's clip page draws besides (the model's class, the device, the
 * frames, the guidance, the parent's record and the clip it continued, each
 * LoRA's sha256), each `undefined` where the record states none.
 */
export interface ClipRecord {
  readonly status: string
  /** The clip, absolute. */
  readonly output: string
  readonly sidecar: string | undefined
  /** The model's name: a store handle (`Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6`). */
  readonly model: string
  /** The class its index names (`WanPipeline`, `Wan ti2v`). */
  readonly modelClass: string | undefined
  readonly prompt: string
  readonly image: string | undefined
  /** `t2v` or `i2v`. */
  readonly mode: string
  readonly seed: number | undefined
  /** `given`, `held` or `generated`. */
  readonly seedSource: string | undefined
  readonly width: number | undefined
  readonly height: number | undefined
  readonly duration: number | undefined
  readonly fps: number | undefined
  /** What the duration at the rate came to, rounded to the pipelines' 4k+1. */
  readonly frames: number | undefined
  readonly steps: number | undefined
  readonly guidance: number | undefined
  readonly codec: string | undefined
  /** How long the run took, in seconds. */
  readonly seconds: number | undefined
  readonly createdAt: string | undefined
  /** `from` (a record run again) or `extend` (a clip continued); `undefined` for a run of its own. */
  readonly parentMove: string | undefined
  /** The clip it came from. */
  readonly parentClip: string | undefined
  /** The record it read. */
  readonly parentDocument: string | undefined
  /** The clip whose last frame it started from. */
  readonly parentContinued: string | undefined
  /** `draft`, `final`, or `undefined`. */
  readonly preset: string | undefined
  readonly loras: readonly ClipLoRA[]
  readonly error: string | undefined
  readonly device: ClipDevice
  /** The engine it ran on (the CLI's 53l): the record's word, else `torch`, as a record from before it ran there. */
  readonly engine: string
  /** The versions that ran, as the record names them, each as written. */
  readonly versions: Readonly<Record<string, string>>
  /** The clip's file name. */
  readonly name: string
}

const { string, number, int, object, array } = JSONValue

/** The last part of a path. */
export function baseName(path: string): string {
  return path.slice(path.lastIndexOf('/') + 1)
}

export const ClipRecord = {
  /** The record `value` is, or `undefined` when it is not a `video.generation` document with its output. */
  read(value: JSONValue): ClipRecord | undefined {
    const doc = object(value)
    const output = string(doc?.output)
    if (!doc || string(doc.object) !== 'video.generation' || output === undefined) return undefined
    const settings = object(doc.settings)
    const model = object(doc.model)
    const parent = object(doc.parent)
    const device = object(doc.device)
    const versions: Record<string, string> = {}
    for (const [key, entry] of Object.entries(object(doc.versions) ?? {})) {
      const text = string(entry)
      if (text !== undefined) versions[key] = text
    }
    return {
      status: string(doc.status) ?? '',
      output,
      sidecar: string(doc.sidecar),
      model: string(model?.name) ?? '',
      modelClass: string(model?.class) || undefined,
      prompt: string(doc.prompt) ?? '',
      image: string(doc.image),
      mode: string(doc.mode) ?? '',
      seed: int(doc.seed),
      seedSource: string(doc.seed_source),
      width: int(settings?.width),
      height: int(settings?.height),
      duration: number(settings?.duration),
      fps: int(settings?.fps),
      frames: int(settings?.frames),
      steps: int(settings?.steps),
      guidance: number(settings?.guidance),
      codec: string(doc.codec),
      seconds: number(doc.seconds),
      createdAt: string(doc.created_at),
      parentMove: string(parent?.move),
      parentClip: string(parent?.clip),
      parentDocument: string(parent?.document),
      parentContinued: string(parent?.continued),
      preset: string(doc.preset),
      loras: (array(doc.loras) ?? []).flatMap((entry) => {
        const lora = object(entry)
        const path = string(lora?.path)
        return path === undefined ? [] : [{ path, scale: number(lora?.scale), sha256: string(lora?.sha256) }]
      }),
      error: string(doc.error),
      device: { device: string(device?.device), dtype: string(device?.dtype), platform: string(device?.platform) },
      engine: string(doc.engine) ?? 'torch',
      versions,
      name: baseName(output),
    }
  },

  /** The record's sidecar: as it names it, else beside its clip. */
  sidecarOf(record: ClipRecord): string {
    return record.sidecar ?? `${record.output}.json`
  },
}

/** A sidecar that was passed over, and why. */
export interface Skipped {
  readonly file: string
  readonly reason: string
}

/**
 * The media library: the records found in folders -- `~/Movies/Apogee`,
 * `~/Music/Apogee`, and any the person adds. The folders are the truth; the
 * app keeps no database of its own. The Mac app's `MediaLibrary`
 * (`BusinessMedia`, `MediaRecords.swift`), which reads the folders with
 * `FileManager`; here they are read through a `FolderReading`, the
 * stand-in's table of the files the CLI wrote (design 1k).
 */
export const MediaLibrary = {
  /** Every record of `object` in `folders` (a shallow walk for `*.json`), newest first, each whose output is still there; and those skipped. */
  scan<Record>(folders: readonly string[], reader: FolderReading, object: string, read: (value: JSONValue) => Record | undefined, output: (record: Record) => string, created: (record: Record) => string | undefined): { readonly records: Record[]; readonly skipped: Skipped[] } {
    const records: Record[] = []
    const skipped: Skipped[] = []
    for (const folder of folders) {
      const files = reader.files(folder)
      const present = new Set(files.map((file) => `${folder}/${file.name}`))
      for (const file of [...files].sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0))) {
        if (!file.name.endsWith('.json')) continue
        const path = `${folder}/${file.name}`
        const value = file.text === undefined ? undefined : JSONValue.parse(file.text)
        if (value === undefined) {
          skipped.push({ file: path, reason: 'not a JSON document' })
          continue
        }
        if (JSONValue.string(JSONValue.object(value)?.object) !== object) continue
        const record = read(value)
        if (record === undefined) {
          skipped.push({ file: path, reason: `a ${object} record without its output` })
          continue
        }
        if (!present.has(output(record))) {
          skipped.push({ file: path, reason: `its file is gone: ${output(record)}` })
          continue
        }
        records.push(record)
      }
    }
    records.sort((a, b) => {
      const [x, y] = [created(a) ?? '', created(b) ?? '']
      return x > y ? -1 : x < y ? 1 : 0
    })
    return { records, skipped }
  },

  clips(folders: readonly string[], reader: FolderReading): { readonly records: ClipRecord[]; readonly skipped: Skipped[] } {
    return MediaLibrary.scan(folders, reader, 'video.generation', ClipRecord.read, (record) => record.output, (record) => record.createdAt)
  },

  /** The parent chain of `record`: each clip it came from, back to the first. */
  chain(record: ClipRecord, records: readonly ClipRecord[]): ClipRecord[] {
    const chain: ClipRecord[] = []
    const seen = new Set([record.output])
    let parent = record.parentClip
    while (parent !== undefined && !seen.has(parent)) {
      const clip = parent
      const found = records.find((candidate) => candidate.output === clip)
      if (!found) break
      chain.push(found)
      seen.add(clip)
      parent = found.parentClip
    }
    return chain
  },

  /**
   * A name for `base`.`ext` among `taken` (the folder's file names) that
   * names nothing yet: a taken name gets a number (`lighthouse-2.mp4`) --
   * nothing is ever written over, a clip's record taking its name too, and
   * any file of the extensions `besides` kept beside it.
   */
  freeName(taken: readonly string[], base: string, ext: string, besides: readonly string[] = []): string {
    const clean = base.replaceAll('/', '-').trim()
    const stem = clean === '' ? 'untitled' : clean
    const names = new Set(taken)
    // Free of a file kept beside it too: a composition's `.srt` while it has captions (the Mac's `freePath(besides:)`, design 1l).
    const free = (name: string) => !names.has(`${name}.${ext}`) && !names.has(`${name}.${ext}.json`) && besides.every((other) => !names.has(`${name}.${other}`))
    let name = stem
    let n = 2
    while (!free(name)) {
      name = `${stem}-${n}`
      n += 1
    }
    return `${name}.${ext}`
  },

  /** A file-name stem from a prompt's first words: `a lighthouse at dusk` is `a-lighthouse-at-dusk`. */
  stem(prompt: string, words = 5): string {
    return prompt
      .toLowerCase()
      .split(/[^\p{L}\p{N}]+/u)
      .filter((word) => word !== '')
      .slice(0, words)
      .join('-')
  },

  /**
   * The command that installs a media runtime -- `train setup --with video`
   * or `voice` -- when a refusal's words name it: what the app asks the
   * person before running, never runs on its own. A refusal naming the
   * rebuild (the CLI's M19) gives `train setup --rebuild --yes --with <set>`:
   * the CLI asks only at a terminal, and the app's sheet is the answer.
   */
  installCommand(text: string): string[] | undefined {
    const at = text.indexOf('apogee train setup')
    if (at < 0) return undefined
    let words = text
      .slice(at)
      .split(/[ \n]+/)
      .filter((word) => word !== '')
      .slice(3)
    const rebuild = words[0] === '--rebuild'
    if (rebuild) words = words.slice(1)
    if (words.length < 2 || words[0] !== '--with') return undefined
    const set = (words[1] ?? '').replace(/^['"`.,;)]+|['"`.,;)]+$/g, '')
    return rebuild ? ['train', 'setup', '--rebuild', '--yes', '--with', set] : ['train', 'setup', '--with', set]
  },

  /** Whether `command` replaces the environment rather than adding to it. */
  rebuilds(command: readonly string[]): boolean {
    return command.includes('--rebuild')
  },

  /** What a rebuild costs, as an install sheet says it. */
  rebuildNote:
    "This replaces Apogee's own Python environment with one on a Python every runtime runs on, downloading every set it held again -- several gigabytes, once. If anything fails, the environment that was there is put back. Nothing is replaced unless you say yes.",
}
