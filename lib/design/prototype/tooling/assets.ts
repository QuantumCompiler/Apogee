import { readFileSync } from 'node:fs'
import { extname, resolve } from 'node:path'
import type { Plugin } from 'vite'

/**
 * `@assets`, the named root of the design's own assets beside the prototype,
 * `lib/design/assets` (design 1q): the logo the gallery's Foundations page
 * draws, `Logo-320.png`, which `lib/src/darwin/scripts/icon.sh` makes from
 * `Logo.png` -- the one artwork, never copied into the prototype. An asset is
 * imported by its full name, `@assets/<file>`, and only by `presentation/app`
 * (the layering check's rule 4), which hands it to the view that draws it.
 * The build serves it as a module whose default export is a `data:` URL of
 * its bytes, so `lib/design/main.html` stays one self-contained file.
 */
export const assetsAlias = '@assets'

/** Where `@assets` points, from the prototype's root (`lib/design/prototype`). */
export function assetsRoot(root: string): string {
  return resolve(root, '../assets')
}

/** The media types an asset may have, by its extension. */
const mediaTypes: Readonly<Record<string, string>> = { '.png': 'image/png' }

/** An asset's module: its bytes as a `data:` URL, the default export. */
export function assetModule(file: string): string {
  const type = mediaTypes[extname(file).toLowerCase()]
  if (type === undefined) throw new Error(`${file} is not an asset the build inlines: it inlines ${Object.keys(mediaTypes).join(', ')}`)
  return `export default ${JSON.stringify(`data:${type};base64,${readFileSync(file).toString('base64')}`)}\n`
}

/** The id Vite knows an asset's module by. */
const prefix = '\0apogee-asset:'

/** The build step (design 1q): serves `@assets/<file>` as its `data:` URL, read each time, so a re-made logo reaches the next build. Both the build and the tests use it. */
export function assets({ root }: { root: string }): Plugin {
  return {
    name: 'apogee-assets',
    enforce: 'pre',
    resolveId(specifier) {
      if (!specifier.startsWith(`${assetsAlias}/`)) return undefined
      return `${prefix}${specifier.slice(assetsAlias.length + 1)}`
    },
    load(id) {
      if (!id.startsWith(prefix)) return undefined
      const file = resolve(assetsRoot(root), id.slice(prefix.length))
      this.addWatchFile(file)
      return assetModule(file)
    },
  }
}
