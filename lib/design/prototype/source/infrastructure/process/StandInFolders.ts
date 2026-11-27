import type { ApogeeMode } from './StandInChild'

/** One file in a folder: its name, and its text where the app reads it (a record); a clip is its name alone. */
export interface FolderFile {
  readonly name: string
  readonly text?: string
}

/** The files a preset says its folders hold, by the folder's absolute path. */
export type Folders = Readonly<Record<string, readonly FolderFile[]>>

/**
 * A file a job writes into a folder as it runs (design 1l): once `after` of
 * its stdout lines have played -- the project the composer saves beside its
 * video after every step -- and `stopped`, what it leaves instead when its
 * Stop ends the job while this is the last file it wrote: the project saved
 * `cancelled`, its finished scenes kept.
 */
export interface FolderWrite {
  readonly after: number
  readonly folder: string
  readonly file: FolderFile
  readonly stopped?: FolderFile
}

/**
 * What the app reads of a folder: its files, as the Mac's `MediaLibrary`
 * lists `~/Movies/Apogee` with `FileManager` (design 1k). A later port
 * reads the disk under the same shape.
 */
export interface FolderReading {
  files(folder: string): readonly FolderFile[]
}

/**
 * What the app reads of a folder and writes into it (design 1n): the Library's
 * editor saves a definition file the CLI reads -- the person's own agent
 * prompt or symphony spec -- as the Mac's view-models write it with
 * Foundation. A later port writes the disk under the same shape.
 */
export interface FolderWriting extends FolderReading {
  write(folder: string, file: FolderFile): void
}

/**
 * The stand-in for the folders the CLI writes into (design 1k): the CLI has
 * no verb that lists the clips it made -- `video generate` writes each
 * clip's `video.generation` record beside it as `<clip>.json`, and the Mac
 * app reads those files -- so a preset hands the stand-in the folders' files,
 * each record the CLI's own document (recorded, or hand-made in its shape),
 * as it hands `StandInChild` its rules. It reads no disk at run time. The
 * folders are the person's files, so they read the same in every mode; a
 * job the stand-in replays may write into them as it runs (`write`, 1l).
 */
export class StandInFolders implements FolderWriting {
  private held: Record<string, readonly FolderFile[]>

  constructor(settings: { readonly folders?: Folders; readonly apogee: ApogeeMode }) {
    this.held = { ...(settings.folders ?? {}) }
  }

  /** The folders as they stand now: the preset's, and what the jobs have written since. */
  get folders(): Folders {
    return this.held
  }

  files(folder: string): readonly FolderFile[] {
    return this.held[folder] ?? []
  }

  /**
   * A file written into `folder` (design 1l): the one of its name replaced,
   * else added -- what a job the stand-in replays writes as it runs, as the
   * composer saves its project after every step. The one state the folders
   * keep, beside the preset's files.
   */
  write(folder: string, file: FolderFile): void {
    const files = this.held[folder] ?? []
    const at = files.findIndex((entry) => entry.name === file.name)
    this.held = { ...this.held, [folder]: at < 0 ? [...files, file] : files.map((entry, index) => (index === at ? file : entry)) }
  }
}
