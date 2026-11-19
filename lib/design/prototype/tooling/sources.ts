import { createHash } from 'node:crypto'
import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join } from 'node:path'

/**
 * What the build reads, relative to the prototype's root: the entry page, the
 * app's source, the build's own configuration and tooling, and the lock file
 * that pins every package. A folder stands for every file under it. 1d adds
 * its fixture roots here.
 */
export const buildInputs = [
  'main.html',
  'package-lock.json',
  'package.json',
  'source',
  'tooling',
  'tsconfig.json',
  'vite.config.ts',
] as const

/** The `<meta>` the build writes into main.html, holding `sourcesHash` of what it was built from. */
export const sourcesMeta = 'apogee-design-sources'

/** Every input file under `root`, by path, sorted. Dot files (.DS_Store) are left out. */
export function inputFiles(root: string): string[] {
  const found: string[] = []
  const walk = (path: string) => {
    let isDirectory: boolean
    try {
      isDirectory = statSync(join(root, path)).isDirectory()
    } catch {
      return // an input not there yet, such as a fixture root before 1d
    }
    if (!isDirectory) {
      found.push(path)
      return
    }
    for (const name of readdirSync(join(root, path))) {
      if (!name.startsWith('.')) walk(`${path}/${name}`)
    }
  }
  for (const input of buildInputs) walk(input)
  return found.sort()
}

/** A SHA-256 over every input file -- its path, its length and its bytes -- in path order: `sha256-<hex>`. */
export function sourcesHash(root: string): string {
  const hash = createHash('sha256')
  for (const file of inputFiles(root)) {
    const bytes = readFileSync(join(root, file))
    hash.update(`${file}\0${bytes.length}\0`)
    hash.update(bytes)
  }
  return `sha256-${hash.digest('hex')}`
}

/** The hash a built main.html says it was built from, if it says. */
export function builtHash(html: string): string | undefined {
  const tag = new RegExp(`<meta\\b[^>]*\\bname="${sourcesMeta}"[^>]*>`).exec(html)?.[0]
  return tag ? /\bcontent="([^"]*)"/.exec(tag)?.[1] : undefined
}

/** Why a built main.html no longer matches the sources under `root`, as a sentence; `undefined` when it does. */
export function staleness(html: string, root: string): string | undefined {
  const remedy = 'Run npm run build in lib/design/prototype, and commit the new main.html'
  const built = builtHash(html)
  if (built === undefined) {
    return `lib/design/main.html carries no ${sourcesMeta} hash, so npm run build did not write it. ${remedy}`
  }
  const now = sourcesHash(root)
  return built === now ? undefined : `lib/design/main.html is stale: it was built from sources ${built}, and they are now ${now}. ${remedy}`
}
