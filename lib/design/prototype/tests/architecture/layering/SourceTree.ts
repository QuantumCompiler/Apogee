import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join } from 'node:path'

/**
 * The files the check reads, by path relative to the prototype's root
 * (lib/design/prototype) -- the real tree, or a planted one in a test.
 * A folder is seen only when a file sits somewhere under it, as git sees it.
 */
export interface SourceTree {
  /** Every file under `directory` ('' for the root), recursively, sorted. */
  files(directory: string): string[]
  /** The names of the folders directly inside `directory`, sorted. */
  folders(directory: string): string[]
  contents(path: string): string | undefined
  isDirectory(path: string): boolean
}

/** What the disk tree never reads: the install, the build's scratch output and Playwright's. */
const skipped = new Set(['node_modules', 'dist', 'test-results', 'playwright-report'])

/** The prototype on disk. Dot files (.DS_Store, .nvmrc) and the skipped folders are left out. */
export class DiskTree implements SourceTree {
  constructor(readonly root: string) {}

  files(directory: string): string[] {
    const found: string[] = []
    const walk = (relative: string) => {
      let names: string[]
      try {
        names = readdirSync(join(this.root, relative))
      } catch {
        return
      }
      for (const name of names) {
        if (name.startsWith('.') || skipped.has(name)) continue
        const path = relative ? `${relative}/${name}` : name
        if (statSync(join(this.root, path)).isDirectory()) walk(path)
        else found.push(path)
      }
    }
    walk(directory)
    return found.sort()
  }

  folders(directory: string): string[] {
    return foldersOf(this.files(directory), directory)
  }

  contents(path: string): string | undefined {
    try {
      return readFileSync(join(this.root, path), 'utf8')
    } catch {
      return undefined
    }
  }

  isDirectory(path: string): boolean {
    return this.files(path).length > 0
  }
}

/** A tree of planted files, for the tests that plant a violation. */
export class PlantedTree implements SourceTree {
  /** Path -> contents. */
  constructor(readonly planted: Record<string, string>) {}

  files(directory: string): string[] {
    const prefix = directory ? `${directory}/` : ''
    return Object.keys(this.planted)
      .filter((path) => path.startsWith(prefix))
      .sort()
  }

  folders(directory: string): string[] {
    return foldersOf(this.files(directory), directory)
  }

  contents(path: string): string | undefined {
    return this.planted[path]
  }

  isDirectory(path: string): boolean {
    return this.files(path).length > 0
  }
}

/** The first path segment under `directory` of every file that sits in a folder there. */
function foldersOf(files: string[], directory: string): string[] {
  const prefix = directory ? `${directory}/` : ''
  const names = new Set<string>()
  for (const file of files) {
    const rest = file.slice(prefix.length).split('/')
    if (rest.length > 1 && rest[0]) names.add(rest[0])
  }
  return [...names].sort()
}
