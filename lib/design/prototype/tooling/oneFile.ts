import { mkdirSync, writeFileSync } from 'node:fs'
import { dirname } from 'node:path'
import type { Plugin } from 'vite'
import { sourcesHash, sourcesMeta } from './sources.ts'

/**
 * The build's last step: main.html carries the hash of the sources it was
 * built from (the staleness check reads it), the bundle is that one file and
 * nothing else, and it is written to `destination` -- lib/design/main.html.
 * vite-plugin-singlefile has already inlined every script and stylesheet.
 */
export function oneFile({ root, destination }: { root: string; destination: string }): Plugin {
  return {
    name: 'apogee-one-file',
    apply: 'build',
    enforce: 'post',
    transformIndexHtml: () => [
      { tag: 'meta', attrs: { name: sourcesMeta, content: sourcesHash(root) }, injectTo: 'head' },
    ],
    writeBundle(_options, bundle) {
      const files = Object.keys(bundle)
      const page = bundle['main.html']
      if (files.length !== 1 || page?.type !== 'asset') {
        this.error(`the build emitted ${files.join(', ')}: it writes one self-contained main.html and nothing else`)
      }
      mkdirSync(dirname(destination), { recursive: true })
      writeFileSync(destination, page.source)
    },
  }
}
