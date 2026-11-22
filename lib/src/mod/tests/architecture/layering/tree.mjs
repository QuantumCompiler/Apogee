// The mod's files as the layering check reads them: every code file of the
// plugin's own (the composition root, the layers, the contract and the tests),
// by its path from lib/src/mod, '/'-separated. node_modules, the vendored API
// declarations and what the engine writes under .claude-plugin/ are not the
// mod's code and are left out.
import { readdirSync, readFileSync } from 'node:fs'
import { dirname, join, relative, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

/** lib/src/mod: this file is tests/architecture/layering/tree.mjs. */
export const MOD_ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..')

/** The folders holding the mod's own code. */
const FOLDERS = ['hooks', 'src', 'types', 'tests']

const CODE = /\.(ts|tsx|mts|cts|js|jsx|mjs|cjs)$/

/** `{ files, api }`: each code file's text by its path, and the API declarations' path. */
export function readMod(root = MOD_ROOT) {
  const files = new Map()
  for (const folder of FOLDERS) {
    for (const path of walk(join(root, folder))) {
      files.set(relative(root, path).split(sep).join('/'), readFileSync(path, 'utf8'))
    }
  }

  return { files, api: join(root, 'vendor', 'claude-code', 'index.d.ts') }
}

function* walk(folder) {
  let entries
  try {
    entries = readdirSync(folder, { withFileTypes: true })
  } catch {
    return
  }
  for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name))) {
    const path = join(folder, entry.name)
    if (entry.isDirectory()) yield* walk(path)
    else if (entry.isFile() && CODE.test(entry.name)) yield path
  }
}
