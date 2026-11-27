import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { logoArtwork } from '../../../source/presentation/app/Models/Logo'
import { assetModule } from '../../../tooling/assets'

/** lib/design/assets/Logo-320.png: this file is tests/presentation/app/Logo.test.ts. */
const file = resolve(dirname(fileURLToPath(import.meta.url)), '../../../../assets/Logo-320.png')

describe("the logo app hands the gallery: lib/design/assets/Logo-320.png, inlined by the build (design 1q)", () => {
  test('a data: URL of the file’s own bytes, so the built file loads nothing', () => {
    expect(logoArtwork).toBe(`data:image/png;base64,${readFileSync(file).toString('base64')}`)
  })

  test('an asset of a kind the build does not inline is refused, naming it', () => {
    expect(() => assetModule('/somewhere/Logo.svg')).toThrow('/somewhere/Logo.svg is not an asset the build inlines: it inlines .png')
  })
})
