import checkAdapters from '@recorded/business/reads/Fixtures/check-adapters.json?raw'
import configGetAdapters from '@recorded/business/reads/Fixtures/config-get-adapters.txt?raw'
import configGetAdaptersNone from '@recorded/business/reads/Fixtures/config-get-adapters-none.txt?raw'
import { describe, expect, test } from 'vitest'
import { AdapterCheck, BackendAdapter } from '../../../source/business/reads/Adapters'
import { JSONValue } from '../../../source/data/protocol/JSONValue'

describe("a backend's adapters, as config get and check state them (the Mac's BackendAdapter, AdapterCheck)", () => {
  test('config-get-adapters.txt: each line a path, and its scale after the last colon when it is not 1; none for an empty answer', () => {
    expect(BackendAdapter.list(configGetAdapters)).toEqual([
      { path: '/Users/someone/.apogee/training/versions/based/v1.lora.gguf', scale: 1 },
      { path: '/Users/someone/extra.lora.gguf', scale: 0.5 },
    ])
    expect(BackendAdapter.list(configGetAdaptersNone)).toEqual([])
    expect(BackendAdapter.argument({ path: '/x.gguf', scale: 0.5 })).toBe('/x.gguf:0.5')
    expect(BackendAdapter.argument({ path: '/x.gguf', scale: 1 })).toBe('/x.gguf')
    expect(BackendAdapter.file({ path: '/a/b/dir/', scale: 1 })).toBe('dir')
  })

  test("check-adapters.json: the doctor's row for each adapter, by `<backend>: <file>`", () => {
    const rows = AdapterCheck.rows(BackendAdapter.list(configGetAdapters), 'based', JSONValue.parse(checkAdapters) ?? null)
    expect(Object.keys(rows)).toContain('/Users/someone/.apogee/training/versions/based/v1.lora.gguf')
    expect(rows['/Users/someone/.apogee/training/versions/based/v1.lora.gguf']?.status).toMatch(/ok|warn|fail|skipped/)
  })
})
