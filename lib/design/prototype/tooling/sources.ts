import { createHash } from 'node:crypto'
import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { tokensAlias, tokensRoot } from './tokens.ts'

/**
 * `@recorded`, the alias for the Mac app's tests (`lib/src/darwin/tests`),
 * whose `Fixtures/` folders hold the CLI's recordings (design 1d). A
 * recording is imported by name with `?raw`, so its text stays byte for byte.
 */
export const recordedAlias = '@recorded'

/** Where `@recorded` points, from the prototype's root (`lib/design/prototype`). */
export function recordedRoot(root: string): string {
  return resolve(root, '../../src/darwin/tests')
}

/**
 * What the build reads, relative to the prototype's root: the entry page, the
 * app's source, the build's own configuration and tooling, the lock file that
 * pins every package, and the design's own fixture root, `fixtures/` (design
 * 1d). A folder stands for every file under it. The recordings `@recorded`
 * reaches are inputs too, but only those `source/` names (`recordingsNamed`),
 * and so is tokens.json, as `@tokens/tokens.json` (`tokenInputs`, design 1e).
 */
export const buildInputs = [
  'fixtures',
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

/** Every file under `path` (a file stands for itself), relative to `root`, unsorted. Dot files (.DS_Store) are left out. */
function filesUnder(root: string, path: string): string[] {
  let isDirectory: boolean
  try {
    isDirectory = statSync(join(root, path)).isDirectory()
  } catch {
    return [] // an input not there yet, such as fixtures/ before a hand-made file
  }
  if (!isDirectory) return [path]
  return readdirSync(join(root, path))
    .filter((name) => !name.startsWith('.'))
    .flatMap((name) => filesUnder(root, `${path}/${name}`))
}

/**
 * The recordings `source/` names, as `@recorded/<path>`, sorted: each
 * `@recorded/…` string in a file under it, its `?raw` left off, that is a
 * file on disk. A preset imports a recording by name, never by a glob (the
 * layering check holds that), so these are what the build can bundle -- and
 * the Swift tests beside them, or a recording only a test reads, never stale
 * the build.
 */
export function recordingsNamed(root: string): string[] {
  const named = new Set<string>()
  for (const file of filesUnder(root, 'source')) {
    const text = readFileSync(join(root, file), 'utf8')
    // The pattern spells `recordedAlias`: a quote, then `@recorded/` and the path up to its query.
    for (const match of text.matchAll(/['"`](@recorded\/[^'"`?\s]+)/g)) {
      const name = match[1]
      if (name === undefined) continue
      try {
        if (statSync(inputPath(root, name)).isFile()) named.add(name)
      } catch {
        // not a file: the build would refuse it first
      }
    }
  }
  return [...named].sort()
}

/**
 * The tokens the build generates CSS and the typed module from (design 1e):
 * tokens.json, the one file outside the prototype the build reads besides
 * the recordings, named by its root.
 */
export const tokenInputs = [`${tokensAlias}/tokens.json`] as const

/** Where an input is on disk: a recording under `@recorded`'s root, the tokens under `@tokens`', anything else under the prototype's. */
export function inputPath(root: string, file: string): string {
  if (file.startsWith(`${recordedAlias}/`)) return join(recordedRoot(root), file.slice(recordedAlias.length + 1))
  if (file.startsWith(`${tokensAlias}/`)) return join(tokensRoot(root), file.slice(tokensAlias.length + 1))
  return join(root, file)
}

/** Every input, by path (a recording as `@recorded/<path>`, the tokens as `@tokens/tokens.json`), sorted. */
export function inputFiles(root: string): string[] {
  return [...buildInputs.flatMap((input) => filesUnder(root, input)), ...recordingsNamed(root), ...tokenInputs].sort()
}

/** A SHA-256 over every input file -- its path, its length and its bytes -- in path order: `sha256-<hex>`. */
export function sourcesHash(root: string): string {
  const hash = createHash('sha256')
  for (const file of inputFiles(root)) {
    const bytes = readFileSync(inputPath(root, file))
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
