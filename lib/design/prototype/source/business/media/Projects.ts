import { JSONValue } from '../../data/protocol/JSONValue'
import { baseName, MediaLibrary } from './MediaRecords'
import type { FolderReading, Skipped } from './MediaRecords'

/** One sentence of a scene's narration as the voice spoke it: where it starts in the narration and how long it ran (the CLI's 50c). */
export interface ProjectSentence {
  readonly text: string
  readonly seconds: number
  readonly offset: number
}

/** One scene of a project, as the composer keeps it. */
export interface ProjectScene {
  readonly index: number
  readonly narration: string
  readonly visual: string
  /** The plan's length for it. */
  readonly seconds: number | undefined
  /** An asset the scene takes or starts from; `undefined` for none. */
  readonly asset: string | undefined
  readonly seed: number | undefined
  /** Its narration's WAV, once spoken, its measured seconds and its sentences. */
  readonly voiceFile: string | undefined
  readonly voiceSeconds: number | undefined
  readonly sentences: readonly ProjectSentence[]
  /** Its clip, once rendered, and the seconds ffprobe read. */
  readonly clipFile: string | undefined
  readonly clipSeconds: number | undefined
  /** Since 52c, in an edited project: the parent's scene it came from (`new` for none), what changed, and whether it moved. */
  readonly from: string | undefined
  readonly changes: readonly string[] | undefined
  readonly moved: boolean
}

/** One edit from the original brief to this project: its instruction, the scenes it re-rolled, and the project it was made from. */
export interface ProjectEdit {
  readonly instruction: string | undefined
  readonly redo: readonly number[]
  readonly from: string | undefined
}

/**
 * A composition's project: the `video.project` document the composer keeps
 * beside its video as `<video>.json`, saved after every step -- the one
 * source of a composition's progress (the CLI's 50c, 50d and 52c). The Mac
 * app's `ProjectRecord` (`BusinessMedia`, `Projects.swift`), with the
 * settings and the plan's attempts the design's page draws besides, each
 * `undefined` where the document states none (design 1l).
 */
export interface ProjectRecord {
  /** `rendering`, `done`, `failed`, `cancelled`; `planned` for a dry run's. */
  readonly status: string
  readonly brief: string
  readonly title: string
  /** The video, absolute. */
  readonly output: string
  readonly scenesDir: string | undefined
  readonly scenes: readonly ProjectScene[]
  readonly captionsFile: string | undefined
  readonly captionsBurned: boolean | undefined
  /** The project it was edited from, `undefined` for an original. */
  readonly parent: string | undefined
  readonly edits: readonly ProjectEdit[]
  /** Since 52c: the parent's scenes an edit left out. */
  readonly removed: readonly number[] | undefined
  readonly error: string | undefined
  readonly created: string | undefined
  readonly platform: string | undefined
  /** The settings: the pipeline and the voice model as store handles, the voice, the crossfade, captions, music and assets. */
  readonly model: string | undefined
  readonly voiceModel: string | undefined
  readonly voice: string | undefined
  readonly crossfade: number | undefined
  readonly captions: boolean
  readonly music: string | undefined
  readonly assets: string | undefined
  /** The backend that planned it, as its `plan.backend` names it; `undefined` where the document names none. */
  readonly planner: string | undefined
  /** The planning calls it took: 1, or 2 after its one correction. */
  readonly attempts: number | undefined
  /** The video's file name. */
  readonly name: string
}

const { string, number, int, bool, array, object } = JSONValue

/**
 * Whether an asset is a still the composer renders a clip from -- png, jpg,
 * jpeg or webp, by its extension in any case -- rather than a clip it takes
 * as it is: the CLI's rule (`compose.cpp`'s `is_still`), never the scene's
 * visual, which the CLI always writes (design 1l).
 */
export const isStill = (name: string): boolean => /\.(png|jpe?g|webp)$/i.test(name)

/**
 * The seconds a scene needs for its narration: its lead in, the narration
 * spoken, the room after it and the next scene's fade (the CLI's
 * `narration_needs`: 0.25 + spoken + 0.25 + crossfade).
 */
export const narrationNeeds = (spoken: number, crossfade: number): number => 0.25 + spoken + 0.25 + crossfade

/** A text the document states, `undefined` for null or an empty string (the CLI's `text_or_null`). */
const text = (value: JSONValue | undefined) => {
  const said = string(value)
  return said === undefined || said === '' ? undefined : said
}

export const ProjectRecord = {
  /** The project `value` is, or `undefined` when it is not a `video.project` document with its output. */
  read(value: JSONValue): ProjectRecord | undefined {
    const doc = object(value)
    const output = string(doc?.output)
    if (!doc || string(doc.object) !== 'video.project' || output === undefined) return undefined
    const settings = object(doc.settings)
    const plan = object(doc.plan)
    const captions = object(doc.captions)
    const removed = array(doc.removed)
    return {
      status: string(doc.status) ?? '',
      brief: string(doc.brief) ?? '',
      title: string(doc.title) ?? '',
      output,
      scenesDir: text(doc.scenes_dir),
      scenes: (array(doc.scenes) ?? []).flatMap((entry): ProjectScene[] => {
        const scene = object(entry)
        const index = int(scene?.index)
        if (!scene || index === undefined) return []
        const voice = object(scene.voice)
        const clip = object(scene.clip)
        const from = string(scene.from) ?? (int(scene.from) === undefined ? undefined : String(int(scene.from)))
        const changes = array(scene.changes)
        return [
          {
            index,
            narration: string(scene.narration) ?? '',
            visual: string(scene.visual) ?? '',
            seconds: number(scene.seconds),
            asset: text(scene.asset),
            seed: int(scene.seed),
            voiceFile: text(voice?.file),
            voiceSeconds: number(voice?.seconds),
            sentences: (array(voice?.sentences) ?? []).flatMap((item) => {
              const sentence = object(item)
              const said = string(sentence?.text)
              return said === undefined ? [] : [{ text: said, seconds: number(sentence?.seconds) ?? 0, offset: number(sentence?.offset) ?? 0 }]
            }),
            clipFile: text(clip?.file),
            clipSeconds: number(clip?.seconds),
            from,
            changes: changes?.flatMap((change) => (string(change) === undefined ? [] : [string(change) as string])),
            moved: bool(scene.moved) ?? false,
          },
        ]
      }),
      captionsFile: text(captions?.file),
      captionsBurned: bool(captions?.burned),
      parent: text(doc.parent),
      edits: (array(doc.edits) ?? []).map((entry) => {
        const edit = object(entry)
        return { instruction: text(edit?.instruction), redo: (array(edit?.redo) ?? []).flatMap((scene) => (int(scene) === undefined ? [] : [int(scene) as number])), from: text(edit?.from) }
      }),
      removed: removed?.flatMap((scene) => (int(scene) === undefined ? [] : [int(scene) as number])),
      error: text(doc.error),
      created: text(doc.created),
      platform: text(doc.platform),
      model: text(settings?.model),
      voiceModel: text(settings?.voice_model),
      voice: text(settings?.voice),
      crossfade: number(settings?.crossfade),
      captions: bool(settings?.captions) ?? true,
      music: text(settings?.music),
      assets: text(settings?.assets),
      planner: text(plan?.backend),
      attempts: int(plan?.attempts),
      name: baseName(output),
    }
  },

  /** Whether the scene takes its asset as it is (a clip, not a still: `isStill`), so nothing renders for it. */
  takesAsItIs(scene: ProjectScene): boolean {
    return scene.asset !== undefined && !isStill(scene.asset)
  },

  /**
   * Whether the composer keeps the scene's clip: spoken, and the clip long
   * enough for what its narration needs, a tenth of a second spared (the
   * CLI's clip pass, `compose.cpp`). A clip an edit carried over whose new
   * narration runs longer is rendered again, its seed held, so it is not
   * made until then.
   */
  keepsClip(scene: ProjectScene, crossfade = 0.5): boolean {
    return scene.voiceFile !== undefined && scene.clipFile !== undefined && (scene.clipSeconds ?? 0) + 0.1 >= narrationNeeds(scene.voiceSeconds ?? 0, crossfade)
  },

  /** Spoken and rendered: nothing of it left to make. A scene that takes its asset as it is has no clip to render; one that starts from a still does, and a clip too short for its narration renders again. */
  isMade(scene: ProjectScene, crossfade = 0.5): boolean {
    return scene.voiceFile !== undefined && (ProjectRecord.keepsClip(scene, crossfade) || ProjectRecord.takesAsItIs(scene))
  },

  /** How many of its scenes are made. */
  made(project: ProjectRecord): number {
    return project.scenes.filter((scene) => ProjectRecord.isMade(scene, project.crossfade ?? 0.5)).length
  },

  /** Whether the edit's own comparison is in the document (52c). */
  hasDiff(project: ProjectRecord): boolean {
    return project.scenes.some((scene) => scene.changes !== undefined) || project.removed !== undefined
  },
}

/** One scene's line in an edit's diff. */
export interface SceneChange {
  readonly index: number
  readonly narration: string
  readonly kind: { readonly case: 'unchanged' } | { readonly case: 'changed'; readonly words: readonly string[] } | { readonly case: 'added' } | { readonly case: 'removed' }
  readonly moved: boolean
}

/** One scene as an edit's comparison reads it: its source, its change words, its narration. */
const said = (project: ProjectRecord) => project.scenes.map((scene) => JSON.stringify([scene.from ?? null, scene.changes ?? [], scene.narration]))

/**
 * The compositions in the folders the CLI writes into: the Mac app's
 * `MediaLibrary` extension in `Projects.swift` (`projects(in:)`, `diff`,
 * `sameChanges`) and `ProjectRecord.beside`, over a `FolderReading`, the
 * stand-in's table of the files the CLI wrote (design 1k), as the clips are.
 */
export const ProjectLibrary = {
  /** Every composition in `folders`, newest first, and the documents passed over: a project is kept beside its video, so it is there when its own document is. */
  projects(folders: readonly string[], reader: FolderReading): { readonly records: ProjectRecord[]; readonly skipped: Skipped[] } {
    return MediaLibrary.scan(folders, reader, 'video.project', ProjectRecord.read, (record) => `${record.output}.json`, (record) => record.created)
  },

  /** The project kept beside `video`, as the composer last saved it, when there is one. */
  beside(video: string, reader: FolderReading): ProjectRecord | undefined {
    const slash = video.lastIndexOf('/')
    const file = reader.files(video.slice(0, slash)).find((entry) => entry.name === `${video.slice(slash + 1)}.json`)
    const value = file?.text === undefined ? undefined : JSONValue.parse(file.text)
    return value === undefined ? undefined : ProjectRecord.read(value)
  },

  /**
   * An edit's diff: from the edited project's own comparison where it
   * carries one (52c), else by comparing the two documents scene by scene in
   * order -- said as a comparison, since the scenes do not name their source.
   */
  diff(edited: ProjectRecord, parent: ProjectRecord | undefined): { readonly changes: SceneChange[]; readonly compared: boolean } {
    if (ProjectRecord.hasDiff(edited)) {
      const changes: SceneChange[] = edited.scenes.map((scene) => ({
        index: scene.index,
        narration: scene.narration,
        kind: scene.from === 'new' ? { case: 'added' } : (scene.changes ?? []).length === 0 ? { case: 'unchanged' } : { case: 'changed', words: scene.changes ?? [] },
        moved: scene.moved,
      }))
      for (const index of edited.removed ?? []) changes.push({ index, narration: parent?.scenes.find((scene) => scene.index === index)?.narration ?? '', kind: { case: 'removed' }, moved: false })
      return { changes, compared: false }
    }
    const before = parent?.scenes ?? []
    const changes: SceneChange[] = []
    for (const scene of edited.scenes) {
      const old = before.find((candidate) => candidate.index === scene.index)
      if (!old) {
        changes.push({ index: scene.index, narration: scene.narration, kind: { case: 'added' }, moved: false })
        continue
      }
      const words: string[] = []
      if (old.narration !== scene.narration) words.push('narration')
      if (old.visual !== scene.visual) words.push('prompt')
      if (old.seconds !== scene.seconds) words.push('duration')
      if (old.asset !== scene.asset) words.push('asset')
      if (old.seed !== scene.seed) words.push('seed')
      changes.push({ index: scene.index, narration: scene.narration, kind: words.length === 0 ? { case: 'unchanged' } : { case: 'changed', words }, moved: false })
    }
    for (const old of before) if (!edited.scenes.some((scene) => scene.index === old.index)) changes.push({ index: old.index, narration: old.narration, kind: { case: 'removed' }, moved: false })
    return { changes, compared: true }
  },

  /**
   * Whether an edit, rendered, changed its project as its preview said it
   * would (GUI 6j): the CLI's edit dry run writes nothing (its 52c), so Render
   * plans the edit again. Compared scene by scene in order -- each scene's
   * source, its change words and its narration -- and the parent's scenes
   * left out: what the diff draws. Never the seeds.
   */
  sameChanges(rendered: ProjectRecord, previewed: ProjectRecord): boolean {
    const gone = (project: ProjectRecord) => JSON.stringify([...(project.removed ?? [])].sort((a, b) => a - b))
    return JSON.stringify(said(rendered)) === JSON.stringify(said(previewed)) && gone(rendered) === gone(previewed)
  },
}
