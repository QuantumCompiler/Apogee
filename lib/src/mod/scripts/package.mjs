// `npm run package`: the plugin a person loads, zipped -- the mod pipeline's
// artifact (the mod's 1d).
//
//   node scripts/package.mjs [--root <mod folder>] [--out <zip>]
//
// The zip holds .claude-plugin/plugin.json, hooks/, src/ and types/ at its
// top, as the engine's own plugin files have `.claude-plugin/` at theirs:
// unzipped into a folder, that folder is the plugin (`claude --plugin-dir`).
// Left out: what the engine lays beside a loaded mod (.claude-plugin/types/),
// the checks' own files (tests/, vendor/, scripts/, package.json and its lock,
// tsconfig.json, node_modules/), and what editors and hosts leave behind (a
// name starting with a dot, `*~`, `*.swp`, `*.log`). A symbolic link is
// refused rather than followed or dropped, a top folder that is one included.
//
// The same tree packs to the same bytes: entries in path order, every one
// dated 1980-01-01 00:00, mode 0644, deflated. Node alone writes it (zlib), so
// the step needs no zip tool on the host.
import { existsSync, lstatSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { dirname, join, relative, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import { crc32, deflateRawSync } from 'node:zlib'

/** lib/src/mod: this file is scripts/package.mjs. */
const MOD_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..')

/** The folders of the plugin a person loads, beside its manifest. */
const FOLDERS = ['hooks', 'src', 'types']

const LINKED = "is a symbolic link: the zip holds the plugin's own files, never what a link points at"

/** What an editor or a host leaves in a folder, never the plugin's. */
const LEFT_BEHIND = /^\.|~$|\.swp$|\.log$/

function fail(message) {
  console.error(`package: ${message}`)
  process.exit(1)
}

function argsOf(argv) {
  const args = { root: MOD_ROOT, out: undefined }
  for (let at = 0; at < argv.length; at += 2) {
    const [flag, value] = [argv[at], argv[at + 1]]
    if ((flag !== '--root' && flag !== '--out') || value === undefined) fail('usage: package.mjs [--root <mod folder>] [--out <zip>]')
    args[flag.slice(2)] = resolve(value)
  }
  args.out ??= join(args.root, 'build', 'apogee-mod.zip')

  return args
}

/** The plugin's files under `root`, by their '/'-separated paths, in path order. */
function pluginFiles(root) {
  // A linked top folder would be read through by the walk (and the manifest's
  // check), so each is refused by name before either looks inside it.
  for (const top of ['.claude-plugin', ...FOLDERS]) {
    if (lstatSync(join(root, top), { throwIfNoEntry: false })?.isSymbolicLink()) fail(`${top} ${LINKED}`)
  }
  const manifest = join(root, '.claude-plugin', 'plugin.json')
  if (!existsSync(manifest)) fail(`${manifest} is missing: a plugin is its manifest first`)
  if (lstatSync(manifest).isSymbolicLink()) fail(`.claude-plugin/plugin.json ${LINKED}`)
  const found = ['.claude-plugin/plugin.json']
  const walk = folder => {
    let entries
    try {
      entries = readdirSync(folder, { withFileTypes: true })
    } catch {
      return
    }
    for (const entry of entries) {
      if (LEFT_BEHIND.test(entry.name)) continue
      const path = join(folder, entry.name)
      const named = relative(root, path).split(sep).join('/')
      if (entry.isSymbolicLink()) fail(`${named} ${LINKED}`)
      if (entry.isDirectory()) walk(path)
      else if (entry.isFile()) found.push(named)
    }
  }
  for (const folder of FOLDERS) walk(join(root, folder))

  return found.sort((a, b) => (a < b ? -1 : a > b ? 1 : 0))
}

/** 1980-01-01 00:00, the first moment a zip can say: (year - 1980) << 9 | month << 5 | day. */
const DOS_DATE = (0 << 9) | (1 << 5) | 1
const DOS_TIME = 0
/** Made by Unix (3), zip 2.0; a regular file, rw-r--r--. */
const MADE_BY = (3 << 8) | 20
const FILE_MODE = 0o100644
/** Bit 11: the name is UTF-8. */
const UTF8 = 0x0800
const DEFLATE = 8

/** A zip of `files`, each `{ name, data }`, in their order. */
function zipOf(files) {
  const locals = []
  const centrals = []
  let offset = 0
  for (const { name, data } of files) {
    const named = Buffer.from(name, 'utf8')
    const packed = deflateRawSync(data, { level: 9 })
    const sum = crc32(data)

    const local = Buffer.alloc(30)
    local.writeUInt32LE(0x04034b50, 0)
    local.writeUInt16LE(20, 4)
    local.writeUInt16LE(UTF8, 6)
    local.writeUInt16LE(DEFLATE, 8)
    local.writeUInt16LE(DOS_TIME, 10)
    local.writeUInt16LE(DOS_DATE, 12)
    local.writeUInt32LE(sum, 14)
    local.writeUInt32LE(packed.length, 18)
    local.writeUInt32LE(data.length, 22)
    local.writeUInt16LE(named.length, 26)
    local.writeUInt16LE(0, 28)
    locals.push(local, named, packed)

    const central = Buffer.alloc(46)
    central.writeUInt32LE(0x02014b50, 0)
    central.writeUInt16LE(MADE_BY, 4)
    central.writeUInt16LE(20, 6)
    central.writeUInt16LE(UTF8, 8)
    central.writeUInt16LE(DEFLATE, 10)
    central.writeUInt16LE(DOS_TIME, 12)
    central.writeUInt16LE(DOS_DATE, 14)
    central.writeUInt32LE(sum, 16)
    central.writeUInt32LE(packed.length, 20)
    central.writeUInt32LE(data.length, 24)
    central.writeUInt16LE(named.length, 28)
    central.writeUInt16LE(0, 30)
    central.writeUInt16LE(0, 32)
    central.writeUInt16LE(0, 34)
    central.writeUInt16LE(0, 36)
    central.writeUInt32LE((FILE_MODE << 16) >>> 0, 38)
    central.writeUInt32LE(offset, 42)
    centrals.push(central, named)

    offset += local.length + named.length + packed.length
  }
  const directory = Buffer.concat(centrals)
  const end = Buffer.alloc(22)
  end.writeUInt32LE(0x06054b50, 0)
  end.writeUInt16LE(0, 4)
  end.writeUInt16LE(0, 6)
  end.writeUInt16LE(files.length, 8)
  end.writeUInt16LE(files.length, 10)
  end.writeUInt32LE(directory.length, 12)
  end.writeUInt32LE(offset, 16)
  end.writeUInt16LE(0, 20)

  return Buffer.concat([...locals, directory, end])
}

const { root, out } = argsOf(process.argv.slice(2))
const names = pluginFiles(root)
const zip = zipOf(names.map(name => ({ name, data: readFileSync(join(root, name)) })))
rmSync(out, { force: true })
mkdirSync(dirname(out), { recursive: true })
writeFileSync(out, zip)
const shown = relative(process.cwd(), out)
console.log(`package: ${shown.startsWith('..') || shown === '' ? out : shown} holds ${names.length} files of the plugin (.claude-plugin/plugin.json, hooks/, src/, types/)`)
