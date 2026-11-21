import type { ApogeeMode } from './StandInChild'

/** One file in a folder: its name, and its text where the app reads it (a record); a clip is its name alone. */
export interface FolderFile {
  readonly name: string
  readonly text?: string
}

/** The files a preset says its folders hold, by the folder's absolute path. */
export type Folders = Readonly<Record<string, readonly FolderFile[]>>

/**
 * What the app reads of a folder: its files, as the Mac's `MediaLibrary`
 * lists `~/Movies/Apogee` with `FileManager` (design 1k). A later port
 * reads the disk under the same shape.
 */
export interface FolderReading {
  files(folder: string): readonly FolderFile[]
}

/**
 * The stand-in for the folders the CLI writes into (design 1k): the CLI has
 * no verb that lists the clips it made -- `video generate` writes each
 * clip's `video.generation` record beside it as `<clip>.json`, and the Mac
 * app reads those files -- so a preset hands the stand-in the folders' files,
 * each record the CLI's own document (recorded, or hand-made in its shape),
 * as it hands `StandInChild` its rules. It reads no disk at run time. The
 * folders are the person's files, so they read the same in every mode.
 */
export class StandInFolders implements FolderReading {
  readonly folders: Folders

  constructor(settings: { readonly folders?: Folders; readonly apogee: ApogeeMode }) {
    this.folders = settings.folders ?? {}
  }

  files(folder: string): readonly FolderFile[] {
    return this.folders[folder] ?? []
  }
}
