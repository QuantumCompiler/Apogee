// `npm run package` (scripts/package.mjs): the plugin a person loads, zipped
// as the pipeline's artifact. Each test packs a tree -- the mod's own, or one
// planted under $TMPDIR -- into a scratch folder and reads the zip back with
// `unzip`, a reader the writer shares nothing with. Run by node (npm run
// test:node).
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdirSync, readFileSync, readdirSync, symlinkSync, writeFileSync, existsSync } from 'node:fs'
import { dirname, join, relative, sep } from 'node:path'
import { after, before, test } from 'node:test'

import { MOD_ROOT, PACKAGE, entriesOf, run, scratch } from './support.mjs'

let space

before(() => {
  space = scratch('apogee-package-')
})

after(() => {
  space?.remove()
})

/** `npm run package` over `root`, its zip written to `out` when given. */
function pack(root, out) {
  return run('node', [PACKAGE, '--root', root, ...(out === undefined ? [] : ['--out', out])])
}

/** A tree laid out under the scratch folder: each path to its text, or to a symbolic link's target as `{ link }`. */
function plant(name, files) {
  const root = join(space.folder, name)
  for (const [path, text] of Object.entries(files)) {
    const at = join(root, path)
    mkdirSync(dirname(at), { recursive: true })
    if (typeof text === 'object') symlinkSync(text.link, at)
    else writeFileSync(at, text)
  }

  return root
}

/** Every file under `folder` of `root`, by its '/'-separated path from `root`. */
function filesUnder(root, folder) {
  const found = []
  const walk = at => {
    for (const entry of readdirSync(at, { withFileTypes: true })) {
      const path = join(at, entry.name)
      if (entry.isDirectory()) walk(path)
      else found.push(relative(root, path).split(sep).join('/'))
    }
  }
  walk(join(root, folder))

  return found
}

const MANIFEST = '{ "name": "planted", "version": "0.0.1", "engines": { "claude-code": ">=2.1.326" } }\n'

test("the mod's zip holds the plugin a person loads, at its top, and nothing else", () => {
  const zip = join(space.folder, 'mod.zip')
  const ran = pack(MOD_ROOT, zip)
  assert.equal(ran.status, 0, ran.stderr)

  const wanted = ['.claude-plugin/plugin.json', ...filesUnder(MOD_ROOT, 'hooks'), ...filesUnder(MOD_ROOT, 'src'), ...filesUnder(MOD_ROOT, 'types')]
    .filter(path => !path.split('/').some(part => part.startsWith('.') && part !== '.claude-plugin'))
    .sort()
  assert.deepEqual([...entriesOf(zip)].sort(), wanted)
  assert.ok(wanted.includes('hooks/register.tsx') && wanted.includes('types/index.d.ts'))
  // In path order, whatever order the host's folders list in, and each a plain
  // rw-r--r-- file dated 1980-01-01 00:00 (`-T` prints it as yyyymmdd.hhmmss),
  // never the pack's time or a file's: a fresh checkout packs to the same bytes.
  const byCodePoint = (a, b) => (a < b ? -1 : a > b ? 1 : 0)
  assert.deepEqual(entriesOf(zip), [...wanted].sort(byCodePoint))
  const listed = run('unzip', ['-ZT', zip]).stdout.split('\n').filter(line => /^[-dl]/.test(line))
  assert.equal(listed.length, wanted.length)
  for (const line of listed) assert.match(line, /^-rw-r--r-- .* 19800101\.000000 /, line)
  assert.equal(ran.stdout.trim(), `package: ${zip} holds ${wanted.length} files of the plugin (.claude-plugin/plugin.json, hooks/, src/, types/)`)
})

test('each entry is the file it was packed from, byte for byte', () => {
  const zip = join(space.folder, 'bytes.zip')
  assert.equal(pack(MOD_ROOT, zip).status, 0)
  const unpacked = join(space.folder, 'bytes')
  const ran = run('unzip', ['-q', zip, '-d', unpacked])
  assert.equal(ran.status, 0, ran.stderr)
  for (const path of entriesOf(zip)) {
    assert.ok(readFileSync(join(unpacked, path)).equals(readFileSync(join(MOD_ROOT, path))), path)
  }
})

test('the same tree packs to the same bytes', () => {
  const one = join(space.folder, 'one.zip')
  const two = join(space.folder, 'two.zip')
  assert.equal(pack(MOD_ROOT, one).status, 0)
  assert.equal(pack(MOD_ROOT, two).status, 0)
  const digest = path => createHash('sha256').update(readFileSync(path)).digest('hex')
  assert.equal(digest(one), digest(two))
})

test("what the engine lays beside the mod, the checks' files and editors' leavings stay out", () => {
  const root = plant('leavings', {
    '.claude-plugin/plugin.json': MANIFEST,
    '.claude-plugin/types/claude-code.d.ts': '// laid by the engine at load\n',
    'hooks/hooks.json': '{ "modules": ["./register.tsx"] }\n',
    'hooks/register.tsx': 'export const register = () => {}\n',
    'hooks/.DS_Store': 'Finder\n',
    'src/data/state/value.ts': 'export const value = 1\n',
    'src/data/state/value.ts~': 'a backup\n',
    'src/data/state/.value.ts.swp': 'a swap file\n',
    'src/data/state/debug.log': 'a log\n',
    'src/.hidden/secret.ts': 'export const hidden = 1\n',
    'types/index.d.ts': 'export {}\n',
    'tests/data/state/value.test.ts': 'test\n',
    'vendor/claude-code/index.d.ts': 'declare module "claude-code" {}\n',
    'node_modules/typescript/package.json': '{}\n',
    'scripts/package.mjs': '// the pipeline\n',
    'package.json': '{}\n',
    'package-lock.json': '{}\n',
    'tsconfig.json': '{}\n',
    '.git/HEAD': 'ref: refs/heads/main\n',
  })
  const zip = join(space.folder, 'leavings.zip')
  const ran = pack(root, zip)
  assert.equal(ran.status, 0, ran.stderr)
  assert.deepEqual([...entriesOf(zip)].sort(), [
    '.claude-plugin/plugin.json',
    'hooks/hooks.json',
    'hooks/register.tsx',
    'src/data/state/value.ts',
    'types/index.d.ts',
  ])
})

test('entries are in full-path order, where a walk of the folders would list them otherwise', () => {
  // A walk visits src/data/state/a/ before a-b.ts ('a' sorts before 'a-b.ts'),
  // but by path a-b.ts comes first ('-' is below '/').
  const root = plant('ordered', {
    '.claude-plugin/plugin.json': MANIFEST,
    'hooks/register.tsx': '\n',
    'src/data/state/a/x.ts': '\n',
    'src/data/state/a-b.ts': '\n',
    'types/index.d.ts': '\n',
  })
  const zip = join(space.folder, 'ordered.zip')
  assert.equal(pack(root, zip).status, 0)
  assert.deepEqual(entriesOf(zip), ['.claude-plugin/plugin.json', 'hooks/register.tsx', 'src/data/state/a-b.ts', 'src/data/state/a/x.ts', 'types/index.d.ts'])
})

test('with no --out, the zip is build/apogee-mod.zip under the tree', () => {
  const root = plant('default', {
    '.claude-plugin/plugin.json': MANIFEST,
    'hooks/hooks.json': '{}\n',
    'hooks/register.tsx': '\n',
    'src/data/state/value.ts': '\n',
    'types/index.d.ts': '\n',
  })
  const ran = pack(root)
  assert.equal(ran.status, 0, ran.stderr)
  assert.equal(entriesOf(join(root, 'build', 'apogee-mod.zip')).length, 5)
})

test('a symbolic link is refused by name, and no zip is written', () => {
  const root = plant('linked', {
    '.claude-plugin/plugin.json': MANIFEST,
    'hooks/hooks.json': '{}\n',
    'hooks/register.tsx': '\n',
    'src/data/state/value.ts': '\n',
    'src/data/state/elsewhere.ts': { link: '/etc/hosts' },
    'types/index.d.ts': '\n',
  })
  const zip = join(space.folder, 'linked.zip')
  const ran = pack(root, zip)
  assert.equal(ran.status, 1)
  assert.equal(ran.stderr.trim(), 'package: src/data/state/elsewhere.ts is a symbolic link: the zip holds the plugin\'s own files, never what a link points at')
  assert.equal(existsSync(zip), false)
})

// A top folder that is itself a link would be read through, packing what it
// points at as the plugin's own: each is refused by name, as a link inside one is.
const TOPS = {
  '.claude-plugin': { 'plugin.json': MANIFEST },
  hooks: { 'hooks.json': '{}\n', 'register.tsx': '\n' },
  src: { 'data/state/value.ts': '\n' },
  types: { 'index.d.ts': '\n' },
}
for (const top of Object.keys(TOPS)) {
  test(`a linked top folder, ${top}, is refused by name, and no zip is written`, () => {
    const name = `linked-top${top}`
    const outside = plant(`${name}-outside`, TOPS[top])
    const files = {}
    for (const [folder, held] of Object.entries(TOPS)) {
      if (folder === top) files[folder] = { link: outside }
      else for (const [path, text] of Object.entries(held)) files[`${folder}/${path}`] = text
    }
    const root = plant(name, files)
    const zip = join(space.folder, `${name}.zip`)
    const ran = pack(root, zip)
    assert.equal(ran.status, 1, ran.stdout)
    assert.equal(ran.stderr.trim(), `package: ${top} is a symbolic link: the zip holds the plugin's own files, never what a link points at`)
    assert.equal(existsSync(zip), false)
  })
}

test('a tree with no manifest is refused, and no zip is written', () => {
  const root = plant('unmanifested', { 'hooks/register.tsx': '\n', 'src/data/state/value.ts': '\n' })
  const zip = join(space.folder, 'unmanifested.zip')
  const ran = pack(root, zip)
  assert.equal(ran.status, 1)
  assert.equal(ran.stderr.trim(), `package: ${join(root, '.claude-plugin', 'plugin.json')} is missing: a plugin is its manifest first`)
  assert.equal(existsSync(zip), false)
})
