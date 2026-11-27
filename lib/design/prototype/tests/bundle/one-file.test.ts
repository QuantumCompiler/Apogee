import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'

/** The committed build, lib/design/main.html: this file is tests/bundle/one-file.test.ts. */
const built = resolve(dirname(fileURLToPath(import.meta.url)), '../../../main.html')

/** What would make a page opened from disk load another file, each as a sentence; empty when it is self-contained. */
function loadsAnotherFile(html: string): string[] {
  const found: string[] = []
  for (const tag of html.match(/<script\b[^>]*>/gi) ?? []) {
    if (/\ssrc\s*=/i.test(tag)) found.push(`${tag} loads a script: a page opened from disk cannot`)
  }
  for (const tag of html.match(/<link\b[^>]*>/gi) ?? []) {
    const href = /\shref\s*=\s*["']?([^"'\s>]+)/i.exec(tag)?.[1]
    if (href && !href.startsWith('data:')) found.push(`${tag} loads ${href}: a page opened from disk cannot`)
  }
  for (const call of html.match(/\bimport\s*\(\s*["'`][^"'`]*["'`]\s*\)/g) ?? []) {
    found.push(`${call} loads a chunk: the build inlines every module`)
  }
  return found
}

describe('the built file is one file', () => {
  test('lib/design/main.html has no <script src>, no stylesheet <link href> and no chunk import()', () => {
    const html = readFileSync(built, 'utf8')
    expect(loadsAnotherFile(html)).toEqual([])
    expect(html).toMatch(/<script type="module"[^>]*>[^<]/)
  })

  test('each way a page loads another file is caught', () => {
    expect(
      loadsAnotherFile(
        '<script type="module" crossorigin src="./assets/main-abc.js"></script>' +
          '<link rel="stylesheet" crossorigin href="./assets/main-abc.css">' +
          '<link rel="icon" href="data:image/png;base64,AAAA">' +
          '<script type="module">const chunk = import("./assets/chunk-abc.js")</script>',
      ),
    ).toEqual([
      '<script type="module" crossorigin src="./assets/main-abc.js"> loads a script: a page opened from disk cannot',
      '<link rel="stylesheet" crossorigin href="./assets/main-abc.css"> loads ./assets/main-abc.css: a page opened from disk cannot',
      'import("./assets/chunk-abc.js") loads a chunk: the build inlines every module',
    ])
  })
})
